/*
 * handshake.c — TLS 1.3 handshake messages (RFC 8446 §4). EXPERIMENTAL /
 * UNAUDITED. See handshake.h.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. The ClientHello parser reads
 * entirely through the bounds-checked wire reader (wire.h), so no access touches
 * memory outside the message buffer; a malformed length anywhere yields a clean
 * 0. Extensions are walked generically — recognized types are decoded, the rest
 * skipped — so an unexpected extension can never cause a mis-parse.
 */
#include "handshake.h"

#ifdef WEBLIB_TLS

#include "wire.h"
#include <string.h>

/* Named constants for the wire values this parser matches. */
#define GROUP_X25519          0x001d
#define SUITE_CHACHA20_SHA256 0x1303
#define SIG_ED25519           0x0807
#define VERSION_TLS13         0x0304

#define EXT_SERVER_NAME         0x0000
#define EXT_SUPPORTED_GROUPS    0x000a
#define EXT_SIGNATURE_ALGS      0x000d
#define EXT_ALPN                0x0010
#define EXT_SUPPORTED_VERSIONS  0x002b
#define EXT_KEY_SHARE           0x0033

/* Return 1 if the u16 list `r` contains `want` (scanning to its end). */
static int list_u16_contains(tls_reader_t *r, uint16_t want) {
    uint16_t v;
    int found = 0;
    while (tls_reader_remaining(r) >= 2) {
        if (!tls_read_u16(r, &v)) {
            return found;
        }
        if (v == want) {
            found = 1;
        }
    }
    return found;
}

int tls_parse_client_hello(const uint8_t *msg, size_t msg_len, tls_client_hello_t *out) {
    tls_reader_t r, cs, exts;
    uint8_t hs_type, sid_len, comp;
    uint32_t body_len;
    uint16_t legacy_version, comp_len16;
    const uint8_t *rand_p;
    tls_reader_t comp_vec;

    if (msg == NULL || out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof *out);

    tls_reader_init(&r, msg, msg_len);

    /* Handshake header: msg_type(1) + uint24 length; the body must be exactly the
     * remaining bytes (no trailing data after the message). */
    if (!tls_read_u8(&r, &hs_type) || hs_type != TLS_HS_CLIENT_HELLO) {
        return 0;
    }
    if (!tls_read_u24(&r, &body_len)) {
        return 0;
    }
    if (body_len != tls_reader_remaining(&r)) {
        return 0;
    }

    /* legacy_version (0x0303, not enforced per §4.1.2) + 32-byte Random. */
    if (!tls_read_u16(&r, &legacy_version)) {
        return 0;
    }
    if (!tls_read_bytes(&r, &rand_p, 32)) {
        return 0;
    }
    memcpy(out->random, rand_p, 32);

    /* legacy_session_id<0..32>. */
    if (!tls_read_u8(&r, &sid_len) || sid_len > 32) {
        return 0;
    }
    if (!tls_read_bytes(&r, &out->session_id, sid_len)) {
        return 0;
    }
    out->session_id_len = sid_len;

    /* cipher_suites<2..2^16-2>: a u16 list of 2-byte suites. */
    if (!tls_read_vector(&r, 2, &cs)) {
        return 0;
    }
    if (tls_reader_remaining(&cs) < 2 || (tls_reader_remaining(&cs) % 2) != 0) {
        return 0;
    }
    out->offers_chacha20_poly1305 = list_u16_contains(&cs, SUITE_CHACHA20_SHA256);

    /* legacy_compression_methods<1..2^8-1>: TLS 1.3 requires exactly one 0 byte. */
    if (!tls_read_vector(&r, 1, &comp_vec)) {
        return 0;
    }
    comp_len16 = (uint16_t)tls_reader_remaining(&comp_vec);
    if (comp_len16 != 1 || !tls_read_u8(&comp_vec, &comp) || comp != 0) {
        return 0;
    }

    /* extensions<8..2^16-1>, the final field. */
    if (!tls_read_vector(&r, 2, &exts)) {
        return 0;
    }
    if (!tls_reader_eof(&r)) {
        return 0;   /* trailing data after the extensions block */
    }

    while (tls_reader_remaining(&exts) > 0) {
        uint16_t ext_type;
        tls_reader_t ext_data;
        if (!tls_read_u16(&exts, &ext_type)) {
            return 0;
        }
        if (!tls_read_vector(&exts, 2, &ext_data)) {
            return 0;
        }

        /* Recognized extensions are decoded strictly: the inner u16 lists must be
         * whole (even byte length) and each extension payload must be consumed in
         * full — a malformed known extension is a parse failure, not a pass. */
        switch (ext_type) {
        case EXT_SUPPORTED_VERSIONS: {
            /* ClientHello form: ProtocolVersion versions<2..254> (u8 length). */
            tls_reader_t versions;
            if (!tls_read_vector(&ext_data, 1, &versions)) {
                return 0;
            }
            if ((tls_reader_remaining(&versions) % 2) != 0) {
                return 0;
            }
            out->offers_tls13 = list_u16_contains(&versions, VERSION_TLS13);
            if (!tls_reader_eof(&ext_data)) {
                return 0;
            }
            break;
        }
        case EXT_SUPPORTED_GROUPS: {
            tls_reader_t groups;
            if (!tls_read_vector(&ext_data, 2, &groups)) {
                return 0;
            }
            if ((tls_reader_remaining(&groups) % 2) != 0) {
                return 0;
            }
            out->offers_x25519 = list_u16_contains(&groups, GROUP_X25519);
            if (!tls_reader_eof(&ext_data)) {
                return 0;
            }
            break;
        }
        case EXT_SIGNATURE_ALGS: {
            tls_reader_t algs;
            if (!tls_read_vector(&ext_data, 2, &algs)) {
                return 0;
            }
            if ((tls_reader_remaining(&algs) % 2) != 0) {
                return 0;
            }
            out->offers_ed25519 = list_u16_contains(&algs, SIG_ED25519);
            if (!tls_reader_eof(&ext_data)) {
                return 0;
            }
            break;
        }
        case EXT_KEY_SHARE: {
            /* KeyShareClientHello: KeyShareEntry client_shares<0..2^16-1>, each
             * { NamedGroup group; opaque key_exchange<1..2^16-1> }. Every entry is
             * consumed (so trailing bytes fail), key_exchange must be non-empty,
             * and the X25519 share is taken if present and exactly 32 bytes. */
            tls_reader_t shares;
            if (!tls_read_vector(&ext_data, 2, &shares)) {
                return 0;
            }
            while (tls_reader_remaining(&shares) > 0) {
                uint16_t group;
                tls_reader_t ke;
                if (!tls_read_u16(&shares, &group)) {
                    return 0;
                }
                if (!tls_read_vector(&shares, 2, &ke)) {
                    return 0;
                }
                if (tls_reader_remaining(&ke) < 1) {
                    return 0;   /* key_exchange<1..> must be non-empty */
                }
                if (group == GROUP_X25519 && tls_reader_remaining(&ke) == 32) {
                    if (!tls_read_bytes(&ke, &out->x25519_key_share, 32)) {
                        return 0;
                    }
                }
            }
            if (!tls_reader_eof(&ext_data)) {
                return 0;
            }
            break;
        }
        case EXT_ALPN: {
            /* application_layer_protocol_negotiation (RFC 7301):
             * ProtocolName protocol_name_list<2..2^16-1>, each opaque<1..2^8-1>.
             * Read every name in full (a truncated one fails) and note whether the
             * list includes "http/1.1"; the actual selection is the caller's. */
            tls_reader_t names;
            if (!tls_read_vector(&ext_data, 2, &names)) {
                return 0;
            }
            if (tls_reader_remaining(&names) < 1) {
                return 0;   /* protocol_name_list<2..> must hold at least one name */
            }
            out->alpn_present = 1;
            while (tls_reader_remaining(&names) > 0) {
                tls_reader_t name;
                if (!tls_read_vector(&names, 1, &name)) {
                    return 0;
                }
                if (tls_reader_remaining(&name) < 1) {
                    return 0;   /* ProtocolName<1..> must be non-empty */
                }
                if (tls_reader_remaining(&name) == TLS_ALPN_HTTP11_LEN) {
                    const uint8_t *p;
                    if (!tls_read_bytes(&name, &p, TLS_ALPN_HTTP11_LEN)) {
                        return 0;
                    }
                    if (memcmp(p, TLS_ALPN_HTTP11, TLS_ALPN_HTTP11_LEN) == 0) {
                        out->alpn_http11 = 1;
                    }
                }
            }
            if (!tls_reader_eof(&ext_data)) {
                return 0;
            }
            break;
        }
        case EXT_SERVER_NAME: {
            /* ServerNameList server_name_list<1..2^16-1>. Each entry is
             * { NameType name_type; HostName host_name<1..2^16-1> } (host_name is
             * the only defined type); read every entry in full so a truncated one
             * fails, and keep the first host_name. */
            tls_reader_t list;
            if (!tls_read_vector(&ext_data, 2, &list)) {
                return 0;
            }
            while (tls_reader_remaining(&list) > 0) {
                uint8_t name_type;
                tls_reader_t name;
                size_t name_len;
                if (!tls_read_u8(&list, &name_type)) {
                    return 0;
                }
                if (!tls_read_vector(&list, 2, &name)) {
                    return 0;
                }
                name_len = tls_reader_remaining(&name);
                if (name_type == 0x00 && out->server_name == NULL && name_len > 0) {
                    if (!tls_read_bytes(&name, &out->server_name, name_len)) {
                        return 0;
                    }
                    out->server_name_len = name_len;
                }
            }
            if (!tls_reader_eof(&ext_data)) {
                return 0;
            }
            break;
        }
        default:
            /* Unknown extension: already consumed via ext_data, skip it. */
            break;
        }
    }

    return 1;
}

/* ---- server handshake message builders --------------------------------- */

int tls_build_server_hello(tls_writer_t *w, const uint8_t random[32],
                           const uint8_t *session_id, size_t session_id_len,
                           const uint8_t x25519_public_key[32]) {
    size_t hdr, sid, exts, sv_ext, ks_ext, ks_entry;

    if (w == NULL || random == NULL || x25519_public_key == NULL) {
        return 0;
    }
    if (session_id == NULL && session_id_len != 0) {
        return 0;
    }
    if (session_id_len > 32) {
        return 0;   /* legacy_session_id_echo<0..32> */
    }

    tls_write_u8(w, TLS_HS_SERVER_HELLO);
    hdr = tls_writer_open_vector(w, 3);

    tls_write_u16(w, 0x0303);                    /* legacy_version */
    tls_write_bytes(w, random, 32);
    sid = tls_writer_open_vector(w, 1);          /* legacy_session_id_echo */
    tls_write_bytes(w, session_id, session_id_len);
    tls_writer_close_vector(w, sid, 1);
    tls_write_u16(w, SUITE_CHACHA20_SHA256);     /* cipher_suite */
    tls_write_u8(w, 0x00);                       /* legacy_compression_method */

    exts = tls_writer_open_vector(w, 2);

    /* supported_versions: in ServerHello this is a single selected_version. */
    tls_write_u16(w, EXT_SUPPORTED_VERSIONS);
    sv_ext = tls_writer_open_vector(w, 2);
    tls_write_u16(w, VERSION_TLS13);
    tls_writer_close_vector(w, sv_ext, 2);

    /* key_share: one KeyShareEntry { group; key_exchange<1..2^16-1> }. */
    tls_write_u16(w, EXT_KEY_SHARE);
    ks_ext = tls_writer_open_vector(w, 2);
    tls_write_u16(w, GROUP_X25519);
    ks_entry = tls_writer_open_vector(w, 2);
    tls_write_bytes(w, x25519_public_key, 32);
    tls_writer_close_vector(w, ks_entry, 2);
    tls_writer_close_vector(w, ks_ext, 2);

    tls_writer_close_vector(w, exts, 2);
    tls_writer_close_vector(w, hdr, 3);
    return tls_writer_ok(w);
}

/* RFC 8446 §4.1.3: the fixed Random a ServerHello carries when it is in fact a
 * HelloRetryRequest — SHA-256("HelloRetryRequest"). A client detects HRR by this
 * exact value, so it must be reproduced byte-for-byte. */
static const uint8_t HELLO_RETRY_REQUEST_RANDOM[32] = {
    0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C, 0x02,
    0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E,
    0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C,
};

int tls_build_hello_retry_request(tls_writer_t *w,
                                  const uint8_t *session_id, size_t session_id_len) {
    size_t hdr, sid, exts, sv_ext, ks_ext;

    if (w == NULL) {
        return 0;
    }
    if (session_id == NULL && session_id_len != 0) {
        return 0;
    }
    if (session_id_len > 32) {
        return 0;   /* legacy_session_id_echo<0..32> */
    }

    tls_write_u8(w, TLS_HS_SERVER_HELLO);        /* HRR shares the ServerHello type */
    hdr = tls_writer_open_vector(w, 3);

    tls_write_u16(w, 0x0303);                    /* legacy_version */
    tls_write_bytes(w, HELLO_RETRY_REQUEST_RANDOM, 32);
    sid = tls_writer_open_vector(w, 1);          /* legacy_session_id_echo */
    tls_write_bytes(w, session_id, session_id_len);
    tls_writer_close_vector(w, sid, 1);
    tls_write_u16(w, SUITE_CHACHA20_SHA256);     /* cipher_suite */
    tls_write_u8(w, 0x00);                       /* legacy_compression_method */

    exts = tls_writer_open_vector(w, 2);

    /* supported_versions: the single selected_version, as in ServerHello. */
    tls_write_u16(w, EXT_SUPPORTED_VERSIONS);
    sv_ext = tls_writer_open_vector(w, 2);
    tls_write_u16(w, VERSION_TLS13);
    tls_writer_close_vector(w, sv_ext, 2);

    /* key_share HRR form (RFC 8446 §4.2.8): KeyShareHelloRetryRequest is just the
     * selected NamedGroup — no key_exchange. */
    tls_write_u16(w, EXT_KEY_SHARE);
    ks_ext = tls_writer_open_vector(w, 2);
    tls_write_u16(w, GROUP_X25519);
    tls_writer_close_vector(w, ks_ext, 2);

    tls_writer_close_vector(w, exts, 2);
    tls_writer_close_vector(w, hdr, 3);
    return tls_writer_ok(w);
}

int tls_build_encrypted_extensions(tls_writer_t *w, const uint8_t *alpn, size_t alpn_len) {
    size_t hdr, exts, ext_data, list, name;
    if (w == NULL) {
        return 0;
    }
    if (alpn == NULL && alpn_len != 0) {
        return 0;
    }
    if (alpn != NULL && (alpn_len < 1 || alpn_len > 255)) {
        return 0;   /* ProtocolName<1..2^8-1> */
    }
    tls_write_u8(w, TLS_HS_ENCRYPTED_EXTENSIONS);
    hdr = tls_writer_open_vector(w, 3);
    exts = tls_writer_open_vector(w, 2);
    if (alpn != NULL) {
        /* One ALPN extension (RFC 7301) with a single selected ProtocolName. */
        tls_write_u16(w, EXT_ALPN);
        ext_data = tls_writer_open_vector(w, 2);
        list = tls_writer_open_vector(w, 2);     /* ProtocolNameList */
        name = tls_writer_open_vector(w, 1);     /* ProtocolName<1..255> */
        tls_write_bytes(w, alpn, alpn_len);
        tls_writer_close_vector(w, name, 1);
        tls_writer_close_vector(w, list, 2);
        tls_writer_close_vector(w, ext_data, 2);
    }
    tls_writer_close_vector(w, exts, 2);
    tls_writer_close_vector(w, hdr, 3);
    return tls_writer_ok(w);
}

int tls_build_certificate(tls_writer_t *w, const uint8_t *cert_der, size_t cert_len) {
    size_t hdr, ctx, list, entry, entry_exts;
    if (w == NULL || cert_der == NULL || cert_len == 0) {
        return 0;   /* cert_data<1..2^24-1> must be non-empty */
    }
    tls_write_u8(w, TLS_HS_CERTIFICATE);
    hdr = tls_writer_open_vector(w, 3);

    ctx = tls_writer_open_vector(w, 1);          /* certificate_request_context = empty */
    tls_writer_close_vector(w, ctx, 1);

    list = tls_writer_open_vector(w, 3);         /* certificate_list<0..2^24-1> */
    entry = tls_writer_open_vector(w, 3);        /* CertificateEntry.cert_data */
    tls_write_bytes(w, cert_der, cert_len);
    tls_writer_close_vector(w, entry, 3);
    entry_exts = tls_writer_open_vector(w, 2);   /* per-certificate extensions: empty */
    tls_writer_close_vector(w, entry_exts, 2);
    tls_writer_close_vector(w, list, 3);

    tls_writer_close_vector(w, hdr, 3);
    return tls_writer_ok(w);
}

int tls_build_certificate_verify(tls_writer_t *w, const uint8_t signature[64]) {
    size_t hdr, sig;
    if (w == NULL || signature == NULL) {
        return 0;
    }
    tls_write_u8(w, TLS_HS_CERTIFICATE_VERIFY);
    hdr = tls_writer_open_vector(w, 3);
    tls_write_u16(w, SIG_ED25519);               /* SignatureScheme */
    sig = tls_writer_open_vector(w, 2);
    tls_write_bytes(w, signature, 64);
    tls_writer_close_vector(w, sig, 2);
    tls_writer_close_vector(w, hdr, 3);
    return tls_writer_ok(w);
}

int tls_build_finished(tls_writer_t *w, const uint8_t verify_data[32]) {
    size_t hdr;
    if (w == NULL || verify_data == NULL) {
        return 0;
    }
    tls_write_u8(w, TLS_HS_FINISHED);
    hdr = tls_writer_open_vector(w, 3);
    tls_write_bytes(w, verify_data, 32);
    tls_writer_close_vector(w, hdr, 3);
    return tls_writer_ok(w);
}

#endif /* WEBLIB_TLS */
