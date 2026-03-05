BUILD_CONFIG="release"

fail()
{
	echo "$1" 1>&2
	exit 1
}

BUILD_ROOT=$PWD/build
SOURCE_ROOT=$PWD
BUILD_FOLDER=$BUILD_ROOT/build-$BUILD_CONFIG
DEPLOY_FOLDER=$BUILD_ROOT/deploy-$BUILD_CONFIG
INSTALLER_FOLDER=$BUILD_ROOT/installer-$BUILD_CONFIG
SDL3_DEPS_DIR=$SOURCE_ROOT/deps/SDL

if [ -n "$CI_VERSION" ]; then
  VERSION=$CI_VERSION
else
  VERSION=`cat $SOURCE_ROOT/app/version.txt`
fi

command -v qmake6 >/dev/null 2>&1 || fail "Unable to find 'qmake6' in your PATH!"
command -v linuxdeployqt >/dev/null 2>&1 || fail "Unable to find 'linuxdeployqt' in your PATH!"
command -v cmake >/dev/null 2>&1 || fail "Unable to find 'cmake' in your PATH!"
command -v gcc >/dev/null 2>&1 || fail "Unable to find 'gcc' in your PATH!"
[ -d "$SDL3_DEPS_DIR" ] || fail "SDL3 source not found at $SDL3_DEPS_DIR - clone it with: git clone --branch release-3.4.2 --depth 1 https://github.com/libsdl-org/SDL.git deps/SDL"

# Build a glibc version shim to bypass linuxdeployqt's host system check.
# linuxdeployqt refuses to run on glibc > 2.35 (Ubuntu 22.04 Jammy), but the
# shim overrides gnu_get_libc_version() via LD_PRELOAD to return "2.35".
echo Building glibc version shim
FAKE_GLIBC_SRC=$(mktemp /tmp/fake_glibc_XXXXXX.c)
FAKE_GLIBC_SO=$(mktemp /tmp/fake_glibc_XXXXXX.so)
echo 'const char *gnu_get_libc_version(void) { return "2.35"; }' > "$FAKE_GLIBC_SRC"
gcc -shared -fPIC -o "$FAKE_GLIBC_SO" "$FAKE_GLIBC_SRC" || fail "Failed to compile glibc shim!"
rm "$FAKE_GLIBC_SRC"

# Extract appimagetool from the linuxdeployqt AppImage. We run linuxdeployqt
# without -appimage so we can strip unwanted bundled libraries before packaging.
echo Extracting appimagetool from linuxdeployqt
APPIMAGETOOL_EXTRACT_DIR=$(mktemp -d)
pushd "$APPIMAGETOOL_EXTRACT_DIR"
LD_PRELOAD="$FAKE_GLIBC_SO" linuxdeployqt --appimage-extract > /dev/null 2>&1
popd
APPIMAGETOOL="$APPIMAGETOOL_EXTRACT_DIR/squashfs-root/usr/bin/appimagetool"
[ -x "$APPIMAGETOOL" ] || fail "Failed to extract appimagetool from linuxdeployqt!"

# Build SDL3 from source if not already built. SDL3 is not bundled as a system
# library, and linuxdeployqt cannot detect it via ldd because we use SDL2-compat.
echo Building SDL3
if [ ! -f "$SDL3_DEPS_DIR/build/libSDL3.so.0" ]; then
  cmake -DSDL_KMSDRM=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_INSTALL_DOCS=OFF \
    -S "$SDL3_DEPS_DIR" -B "$SDL3_DEPS_DIR/build" || fail "SDL3 cmake configure failed!"
  cmake --build "$SDL3_DEPS_DIR/build" -j$(nproc) || fail "SDL3 build failed!"
fi

echo Cleaning output directories
rm -rf $BUILD_FOLDER
rm -rf $DEPLOY_FOLDER
rm -rf $INSTALLER_FOLDER
mkdir -p $BUILD_ROOT
mkdir $BUILD_FOLDER
mkdir $DEPLOY_FOLDER
mkdir $INSTALLER_FOLDER

echo Configuring the project
pushd $BUILD_FOLDER
# Building with Wayland support will cause linuxdeployqt to include libwayland-client.so in the AppImage.
# Since we always use the host implementation of EGL, this can cause libEGL_mesa.so to fail to load due
# to missing symbols from the host's version of libwayland-client.so that aren't present in the older
# version of libwayland-client.so from our AppImage build environment. When this happens, EGL fails to
# work even in X11. To avoid this, we will disable Wayland support for the AppImage.
#
# We disable DRM support because linuxdeployqt doesn't bundle the appropriate libraries for Qt EGLFS.
qmake6 $SOURCE_ROOT/moonlight-qt.pro CONFIG+=disable-wayland CONFIG+=disable-libdrm PREFIX=$DEPLOY_FOLDER/usr DEFINES+=APP_IMAGE || fail "Qmake failed!"
popd

echo Compiling Moonlight in $BUILD_CONFIG configuration
pushd $BUILD_FOLDER
make -j$(nproc) $(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]') || fail "Make failed!"
popd

echo Deploying to staging directory
pushd $BUILD_FOLDER
make install || fail "Make install failed!"
popd

# We need to manually place SDL3 in our AppImage, since linuxdeployqt
# cannot see the dependency via ldd when it looks at SDL2-compat.
echo Staging SDL3 library
mkdir -p $DEPLOY_FOLDER/usr/lib
cp -L "$SDL3_DEPS_DIR/build/libSDL3.so.0" $DEPLOY_FOLDER/usr/lib/ || fail "Failed to stage SDL3!"

echo Deploying dependencies
pushd $INSTALLER_FOLDER
LD_PRELOAD="$FAKE_GLIBC_SO" VERSION=$VERSION linuxdeployqt \
  $DEPLOY_FOLDER/usr/share/applications/com.moonlight_stream.Moonlight.desktop \
  -qmake=qmake6 -qmldir=$SOURCE_ROOT/app/gui -extra-plugins=tls \
  -executable=$DEPLOY_FOLDER/usr/lib/libSDL3.so.0 || fail "linuxdeployqt failed!"
popd

# Remove bundled Qt libraries so the AppImage uses the system Qt on the target.
# Bundling Qt causes version mismatches when the target system has a newer Qt.
echo Removing bundled Qt libraries
rm -f $DEPLOY_FOLDER/usr/lib/libQt6*.so*

# Remove bundled OpenSSL so the AppImage uses the system libssl/libcrypto.
# Bundling OpenSSL causes version mismatches with the system's libcurl.
echo Removing bundled OpenSSL libraries
rm -f $DEPLOY_FOLDER/usr/lib/libssl.so.3 $DEPLOY_FOLDER/usr/lib/libcrypto.so.3

echo Creating AppImage
LD_PRELOAD="$FAKE_GLIBC_SO" VERSION=$VERSION "$APPIMAGETOOL" \
  $DEPLOY_FOLDER \
  $INSTALLER_FOLDER/Moonlight-$VERSION-x86_64.AppImage || fail "appimagetool failed!"

rm -f "$FAKE_GLIBC_SO"
rm -rf "$APPIMAGETOOL_EXTRACT_DIR"

echo Build successful