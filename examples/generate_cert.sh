#!/bin/bash
# Script to generate self-signed SSL certificate for testing

CERT_DIR="certs"
CERT_FILE="${CERT_DIR}/server.crt"
KEY_FILE="${CERT_DIR}/server.key"

# Create certs directory if it doesn't exist
mkdir -p "${CERT_DIR}"

echo "Generating self-signed SSL certificate..."

# Generate private key and certificate in one command
openssl req -x509 -newkey rsa:4096 -keyout "${KEY_FILE}" -out "${CERT_FILE}" \
    -days 365 -nodes -subj "/C=US/ST=State/L=City/O=Organization/CN=localhost"

if [ $? -eq 0 ]; then
    echo "Certificate generated successfully!"
    echo "Certificate: ${CERT_FILE}"
    echo "Private Key: ${KEY_FILE}"
    echo ""
    echo "Note: This is a self-signed certificate for testing only."
    echo "Browsers will show a security warning when accessing HTTPS endpoints."
else
    echo "Failed to generate certificate!"
    exit 1
fi
