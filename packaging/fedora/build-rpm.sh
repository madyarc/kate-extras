#!/bin/sh
# Builds an RPM from the checkout this script sits in, without needing a release tag.
# Needs: sudo dnf install rpm-build rpmdevtools
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
spec="$here/kate-extras.spec"
version=$(rpmspec -q --srpm --qf '%{version}' "$spec")
topdir="$repo/build-rpm"

mkdir -p "$topdir/SOURCES"
git -C "$repo" archive --format=tar.gz --prefix="kate-extras-$version/" \
    -o "$topdir/SOURCES/kate-extras-$version.tar.gz" HEAD
spectool -g -s 1,2 -C "$topdir/SOURCES" "$spec"

sudo dnf builddep -y "$spec"
rpmbuild -bb --define "_topdir $topdir" "$spec"

echo
echo "Built:"
find "$topdir/RPMS" -name '*.rpm'
echo "Install with: sudo dnf install $topdir/RPMS/*/kate-extras-*.rpm"
