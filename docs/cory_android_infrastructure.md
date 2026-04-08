# Cory (Orderfile) Android CI: full assets vs APK-only

This document describes how to use the **Bountynet / Cory** Android build infrastructure: producing **all vendored assets plus the debug APK**, and a **shorter path when the APK alone is enough**. It assumes the **Cory** tree (NDK sample app and `third_party` layout) is available on the machine that runs the commands—typically a Linux EC2 host with Docker (`sudo`), AWS CLI v2, and Android-related paths already set up elsewhere in your runbook.

---

## A) Full run: all assets (Node bundle, ripgrep, app, reports)

**Goal:** Regenerate or verify everything `scripts/build_all_assets.py` orchestrates, including the Android Node artifact copy into `third_party/node24-android`, `cargo-ndk` ripgrep, and Gradle `:app:assembleDebug`.

### On a host with the Cory repo and Android SDK / NDK configured

1. Ensure `ANDROID_SDK_ROOT` or `ANDROID_HOME` points at the SDK (same as local Gradle / NDK expectations in Cory).
2. From the Cory repository root:

   ```bash
   python3 scripts/build_all_assets.py
   ```

3. Optional checks:

   ```bash
   python3 scripts/build_all_assets.py --doctor-only
   ```

**Artifacts (expected paths under Cory root):**

- `third_party/node24-android/bin/arm64-v8a/node`
- `third_party/node24-android/lib/arm64-v8a/liblibnode.a`
- `third_party/ripgrep/target/aarch64-linux-android/release/rg`
- `third_party/ndk-busybox-ref/libs/arm64-v8a/busybox` (when app phase runs)
- `rust/cory_rust/target/aarch64-linux-android/debug/libcory_rust.a`
- `app/build/outputs/apk/debug/app-debug.apk`
- `build-all-assets-manifest.txt`, `build-all-assets-report.txt` on success

### Using the Docker image (matches CodeBuild environment)

Build the image from Cory with `docker/android-build/Dockerfile` and the repository root as context. Run the container with the Cory tree bind-mounted to `/workspace/Cory`. The image entrypoint invokes `scripts/build_all_assets.py`; pass flags after the image name if you need skips (see section B).

### Using AWS CodeBuild (manual S3 source)

Deploy the CloudFormation template `infrastructure/cloudformation/cory-android-ci-manual-s3.yaml` (stack name used in operations: `cory-android-ci-manual`). The CodeBuild project uses `codebuild/android-full/buildspec.yml`, which runs `build_all_assets.py` then `codebuild/android-full/post_build_publish.sh` (public S3 sync and optional Device Farm when not skipped).

On the EC2 trigger host, after the stack exists and ECR contains `bountynet/ndk-nodejs:latest`:

```bash
bash scripts/ec2_start_codebuild.sh
```

That script packs a trimmed source tree, uploads `cory-source.zip` to the stack’s source bucket, and starts the CodeBuild project. **If the account’s CodeBuild service is suspended,** use the same Python/Docker flows on EC2 instead; the scripts and buckets remain valid for manual publish (below).

### Publishing built artifacts to the public S3 prefix (manual)

From Cory root, with the **public artifacts bucket name** taken from the stack output `PublicArtifactsBucketName`:

```bash
export CORY_ROOT="$(pwd)"
export CORY_PUBLIC_ARTIFACTS_BUCKET="<PublicArtifactsBucketName>"
export CORY_ARTIFACT_PREFIX="builds/<your-label>/"
# export CORY_SKIP_DEVICE_FARM=1   # optional
bash codebuild/android-full/post_build_publish.sh
```

Object layout under the prefix mirrors CodeBuild: `apk/`, `native/`, `reports/`, `meta/` as produced by the script.

---

## B) Optimized run: debug APK only

**Goal:** Skip long phases when `third_party/node24-android` and the ripgrep binary are already present and consistent.

From Cory root, with SDK/NDK env set:

```bash
python3 scripts/build_all_assets.py --skip-node --skip-ripgrep
```

That still runs the Rust/app portions required for the APK as wired in `build_all_assets.py`. If you only need Gradle and trust all native inputs:

```bash
./gradlew :app:assembleDebug --console=plain
```

Output APK path:

```text
app/build/outputs/apk/debug/app-debug.apk
```

In Docker, pass the same skips to the entrypoint (arguments are forwarded to `build_all_assets.py`):

```bash
docker run --rm -v "$PWD":/workspace/Cory <ecr-or-local-image> --skip-node --skip-ripgrep
```

For CodeBuild, set environment variables on the project or in `buildspec.yml`:

- `CORY_SKIP_NODE=1`
- `CORY_SKIP_RIPGREP=1`

(Leave `CORY_SKIP_APP` unset unless you intentionally skip the APK.)

---

## Supporting scripts and templates (Cory tree)

| Item | Purpose |
|------|---------|
| `infrastructure/cloudformation/cory-android-ci-manual-s3.yaml` | Manual CodeBuild + S3 source + public artifact bucket |
| `codebuild/android-full/buildspec.yml` | CodeBuild phases (install AWS CLI, doctor, full build, post_publish) |
| `codebuild/android-full/post_build_publish.sh` | Stage APK/native copies; `aws s3 sync` to public bucket; Device Farm when enabled |
| `scripts/pack_codebuild_source.sh` | Rsync trimmed tree + zip for large EC2 working trees |
| `scripts/trigger_cory_codebuild.sh` | Upload zip; `aws codebuild start-build --source-version …` |
| `scripts/ec2_start_codebuild.sh` | Resolve stack outputs; pack/trigger from `/home/cory/Cory` layout |

---

## Notes

- **Device Farm** (region `us-west-2`) is optional in `post_build_publish.sh`; set `CORY_SKIP_DEVICE_FARM=1` to upload only. Fuzz parameters for `schedule-run` must use **string** values for numeric fields when using AWS CLI v2 (e.g. `eventCount` as `"20"`).
- **Account issues:** If `StartBuild` returns `AccountSuspendedException`, only AWS can restore CodeBuild; local or Docker builds still apply.
- **Security:** Do not rely on Device Farm default project environment variables for secrets; rotate any keys exposed in run configuration.
