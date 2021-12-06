echo "Building Pyjion with .NET  $DOTNET_VERSION"
ARCH=`uname -m`
if [$ARCH = "x86_64"]; then
    ARCH="x64"
fi
yum install -y wget && yum clean all
wget -q https://dotnetcli.azureedge.net/dotnet/Sdk/${DOTNET_VERSION}/dotnet-sdk-${DOTNET_VERSION}-linux-${ARCH}.tar.gz
mkdir -p /tmp/dotnet && tar zxf dotnet-sdk-${DOTNET_VERSION}-linux-${ARCH}.tar.gz -C /tmp/dotnet
