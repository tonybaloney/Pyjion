from skbuild import setup
from packaging.version import LegacyVersion
from skbuild.exceptions import SKBuildError
from skbuild.cmaker import get_cmake_version

# Add CMake as a build requirement if cmake is not installed or is too low a version
setup_requires = []
try:
    if LegacyVersion(get_cmake_version()) < LegacyVersion("3.2"):
        setup_requires.append('cmake')
except SKBuildError:
    setup_requires.append('cmake')

with open("README.md", "r") as fh:
    long_description = fh.read()

with open("CHANGELOG.md", "r") as fh:
    long_description += "\n\n" + fh.read()

setup(
    name='pyjion',
    version='0.14.0',
    description='A JIT compiler wrapper for CPython',
    author='Anthony Shaw and Microsoft',
    author_email='anthonyshaw@apache.org',
    url='https://github.com/tonybaloney/Pyjion',
    license='MIT',
    packages=['pyjion'],
    package_dir={'': 'src'},
    setup_requires=setup_requires,
    extras_require={
        'dis': ["rich", "distorm3"]
    },
    python_requires='>=3.9',
    include_package_data=True,
    long_description=long_description,
    long_description_content_type="text/markdown",
    classifiers=[
        "Development Status :: 4 - Beta",
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ]
)
