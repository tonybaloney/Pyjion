#!/bin/bash
set -e -x

cd /github/workspace/
alias python3.9=/opt/python/cp39-cp39/bin/python
python3.9 -m pip install --upgrade --no-cache-dir pip
python3.9 -m pip install auditwheel
python3.9 -m pip install .
python3.9 -m pip wheel . -w ./dist --no-deps
find . -type f -iname "*-linux*.whl" -execdir sh -c "auditwheel repair '{}' -w ./ --plat 'manylinux2014_x86_64' || { echo 'Repairing wheels failed.'; auditwheel show '{}'; exit 1; }" \;
echo "Succesfully built wheels:"
find . -type f -iname "*-manylinux*.whl"