# Publishing the GitHub Package

There are **three ways** to publish the Docker package to GitHub Container Registry:

## Option 1: Automatic (Recommended) ✅

The package will be **automatically built and published** when you create a new release or push a version tag.

### Trigger on Next Release:
The workflow is already set up! It will run automatically when you:
- Create a new release on GitHub
- Push a git tag matching `v*.*.*` pattern

### Manual Trigger via GitHub Actions:
1. Go to: https://github.com/kamrankhan78694/modern-c-web-library/actions/workflows/docker-publish.yml
2. Click "Run workflow" button (top right)
3. Select branch: `main`
4. Click "Run workflow"
5. Wait ~5 minutes for build to complete
6. Package will be available at: https://github.com/kamrankhan78694/modern-c-web-library/pkgs/container/modern-c-web-library

**This is the easiest method!** ⭐

---

## Option 2: Using the Publish Script

If you have Docker running locally:

```bash
# Start Docker Desktop first
open -a Docker  # macOS

# Then run the publish script
./publish-package.sh 0.3.0
```

You'll need a GitHub Personal Access Token with `write:packages` permission:
1. Go to: https://github.com/settings/tokens/new
2. Select scopes: `write:packages`, `read:packages`
3. Generate token
4. Use it when the script prompts

---

## Option 3: Manual Docker Commands

```bash
# 1. Start Docker
open -a Docker  # macOS

# 2. Build the image
docker build -f Dockerfile.release \
  -t ghcr.io/kamrankhan78694/modern-c-web-library:v0.3.0 \
  -t ghcr.io/kamrankhan78694/modern-c-web-library:latest \
  .

# 3. Login to GitHub Container Registry
echo $GITHUB_TOKEN | docker login ghcr.io -u kamrankhan78694 --password-stdin

# 4. Push the images
docker push ghcr.io/kamrankhan78694/modern-c-web-library:v0.3.0
docker push ghcr.io/kamrankhan78694/modern-c-web-library:latest
```

---

## Quick Start: Use GitHub Actions (Easiest!)

**Right now, you can publish immediately:**

1. Go to: https://github.com/kamrankhan78694/modern-c-web-library/actions/workflows/docker-publish.yml

2. Click **"Run workflow"** dropdown (top right)

3. Click **"Run workflow"** button

4. Wait ~5 minutes

5. Your package will be live! 🎉

**Check status at:**
- Workflow: https://github.com/kamrankhan78694/modern-c-web-library/actions
- Package: https://github.com/kamrankhan78694/modern-c-web-library/pkgs/container/modern-c-web-library

---

## After Publishing

Once published, users can use it:

```bash
# Pull and run
docker pull ghcr.io/kamrankhan78694/modern-c-web-library:latest
docker run -p 8080:8080 ghcr.io/kamrankhan78694/modern-c-web-library:latest

# Open http://localhost:8080 in browser
```

---

## Troubleshooting

**If GitHub Actions fails:**
- Check repository settings → Actions → General → Workflow permissions
- Enable "Read and write permissions"
- Enable "Allow GitHub Actions to create and approve pull requests"

**If manual push fails:**
- Make sure your token has `write:packages` scope
- Verify you're logged in: `docker login ghcr.io`
- Check repository is public or token has repo access

---

## Package Visibility

After first publish, make the package public:
1. Go to: https://github.com/users/kamrankhan78694/packages/container/modern-c-web-library/settings
2. Scroll to "Danger Zone"
3. Click "Change visibility"
4. Select "Public"
5. Confirm

This allows anyone to pull the package without authentication.
