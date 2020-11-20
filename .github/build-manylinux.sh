#!/bin/bash
set -e -x

cd /github/workspace/

/opt/python/cp39-cp39/bin/pip install --upgrade --no-cache-dir pip
/opt/python/cp39-cp39/bin/pip install auditwheel
/opt/python/cp39-cp39/bin/pip install .
/opt/python/cp39-cp39/bin/pip wheel . -w ./dist --no-deps
find . -type f -iname "*-linux*.whl" -execdir sh -c "auditwheel repair '{}' -w ./ --plat 'manylinux2014_x86_64' || { echo 'Repairing wheels failed.'; auditwheel show '{}'; exit 1; }" \;
echo "Succesfully built wheels:"
find . -type f -iname "*-manylinux*.whl"