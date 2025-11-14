#!/bin/bash
# Script to manually build and push Docker package to GitHub Container Registry
# Usage: ./publish-package.sh [version]

set -e

VERSION=${1:-"0.3.0"}
REGISTRY="ghcr.io"
IMAGE_NAME="kamrankhan78694/modern-c-web-library"
FULL_IMAGE="${REGISTRY}/${IMAGE_NAME}"

echo "🚀 Building and Publishing Modern C Web Library Package"
echo "=================================================="
echo "Version: v${VERSION}"
echo "Registry: ${REGISTRY}"
echo "Image: ${IMAGE_NAME}"
echo ""

# Check if Docker is running
if ! docker info > /dev/null 2>&1; then
    echo "❌ Error: Docker daemon is not running"
    echo "Please start Docker and try again"
    exit 1
fi

# Build the image
echo "📦 Building Docker image..."
docker build -f Dockerfile.release \
    -t ${FULL_IMAGE}:v${VERSION} \
    -t ${FULL_IMAGE}:latest \
    --build-arg VERSION=${VERSION} \
    .

echo "✅ Build complete!"
echo ""

# Login to GitHub Container Registry
echo "🔐 Logging in to GitHub Container Registry..."
echo "Please enter your GitHub Personal Access Token (with packages:write permission):"
echo "(You can create one at: https://github.com/settings/tokens/new?scopes=write:packages,read:packages)"
echo ""

# Try to use existing token or prompt for new one
if [ -z "$GITHUB_TOKEN" ]; then
    read -sp "GitHub Token: " GITHUB_TOKEN
    echo ""
fi

echo "$GITHUB_TOKEN" | docker login ${REGISTRY} -u kamrankhan78694 --password-stdin

echo "✅ Login successful!"
echo ""

# Push the images
echo "📤 Pushing Docker images..."
docker push ${FULL_IMAGE}:v${VERSION}
docker push ${FULL_IMAGE}:latest

echo ""
echo "🎉 Package published successfully!"
echo ""
echo "📦 Your package is now available at:"
echo "   ${FULL_IMAGE}:v${VERSION}"
echo "   ${FULL_IMAGE}:latest"
echo ""
echo "🚀 Users can pull it with:"
echo "   docker pull ${FULL_IMAGE}:latest"
echo "   docker pull ${FULL_IMAGE}:v${VERSION}"
echo ""
echo "🌐 View package at:"
echo "   https://github.com/kamrankhan78694/modern-c-web-library/pkgs/container/modern-c-web-library"
echo ""
