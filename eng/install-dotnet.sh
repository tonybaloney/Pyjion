echo "Building Pyjion with .NET  $DOTNET_VERSION"
ARCH=`uname -m`
yum install -y wget && yum clean all
wget -q https://dotnetcli.azureedge.net/dotnet/Sdk/${DOTNET_VERSION}/dotnet-sdk-${DOTNET_VERSION}-linux-${ARCH}.tar.gz
mkdir -p /tmp/dotnet && tar zxf dotnet-sdk-${DOTNET_VERSION}-linux-${ARCH}.tar.gz -C /tmp/dotnet
