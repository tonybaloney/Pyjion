ARG DOTNET_VERSION=5.0.202
FROM ubuntu:latest
ARG DOTNET_VERSION
RUN echo "Building Pyjion with .NET  $DOTNET_VERSION"
COPY . /src
WORKDIR /src

ENV TZ=Europe/London
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get -y update && apt-get install -y software-properties-common && apt-get -y update \
    && add-apt-repository ppa:deadsnakes/ppa && apt-get -y update \
    && apt-get install -y wget bison unzip bzip2 cmake llvm-9 clang-9 autoconf automake \
     libtool build-essential curl python3-pip ninja-build git lldb-6.0 liblldb-6.0-dev \
     libunwind8 libunwind8-dev gettext libicu-dev liblttng-ust-dev \
     libssl-dev libnuma-dev libkrb5-dev zlib1g-dev \
     libc6 libgcc1 libgl1 libglib2.0-0 libice6 libsm6 libstdc++6 libx11-6 libxext6 libxrender1 \
     && apt-get install -y python3.9 python3.9-dev python3.9-distutils python3.9-venv \
     && apt-get clean -y && rm -rf /var/lib/apt/lists/*
RUN wget https://dotnetcli.azureedge.net/dotnet/Sdk/${DOTNET_VERSION}/dotnet-sdk-${DOTNET_VERSION}-linux-x64.tar.gz
RUN mkdir -p dotnet && tar zxf dotnet-sdk-${DOTNET_VERSION}-linux-x64.tar.gz -C dotnet
ENV DOTNET_ROOT=/src/dotnet
RUN chmod +x /src/.github/build-manylinux.sh
ENTRYPOINT ["/src/.github/build-manylinux.sh"]
