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

        switch (ext_type) {
        case EXT_SUPPORTED_VERSIONS: {
            /* ClientHello form: ProtocolVersion versions<2..254> (u8 length). */
            tls_reader_t versions;
            if (!tls_read_vector(&ext_data, 1, &versions)) {
                return 0;
            }
            out->offers_tls13 = list_u16_contains(&versions, VERSION_TLS13);
            break;
        }
        case EXT_SUPPORTED_GROUPS: {
            tls_reader_t groups;
            if (!tls_read_vector(&ext_data, 2, &groups)) {
                return 0;
            }
            out->offers_x25519 = list_u16_contains(&groups, GROUP_X25519);
            break;
        }
        case EXT_SIGNATURE_ALGS: {
            tls_reader_t algs;
            if (!tls_read_vector(&ext_data, 2, &algs)) {
                return 0;
            }
            out->offers_ed25519 = list_u16_contains(&algs, SIG_ED25519);
            break;
        }
        case EXT_KEY_SHARE: {
            /* KeyShareClientHello: KeyShareEntry client_shares<0..2^16-1>, each
             * { NamedGroup group; opaque key_exchange<1..2^16-1> }. Take the
             * X25519 share if present and exactly 32 bytes. */
            tls_reader_t shares;
            if (!tls_read_vector(&ext_data, 2, &shares)) {
                return 0;
            }
            while (tls_reader_remaining(&shares) >= 4) {
                uint16_t group;
                tls_reader_t ke;
                if (!tls_read_u16(&shares, &group)) {
                    return 0;
                }
                if (!tls_read_vector(&shares, 2, &ke)) {
                    return 0;
                }
                if (group == GROUP_X25519 && tls_reader_remaining(&ke) == 32) {
                    if (!tls_read_bytes(&ke, &out->x25519_key_share, 32)) {
                        return 0;
                    }
                }
            }
            break;
        }
        case EXT_SERVER_NAME: {
            /* ServerNameList server_name_list<1..2^16-1>; first entry, host_name. */
            tls_reader_t list;
            uint8_t name_type;
            if (!tls_read_vector(&ext_data, 2, &list)) {
                return 0;
            }
            if (!tls_read_u8(&list, &name_type)) {
                return 0;
            }
            if (name_type == 0x00) {   /* host_name */
                tls_reader_t host;
                size_t host_len;
                if (!tls_read_vector(&list, 2, &host)) {
                    return 0;
                }
                host_len = tls_reader_remaining(&host);
                if (host_len > 0) {
                    if (!tls_read_bytes(&host, &out->server_name, host_len)) {
                        return 0;
                    }
                    out->server_name_len = host_len;
                }
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

#endif /* WEBLIB_TLS */
