echo "Building Pyjion with .NET  $DOTNET_VERSION"
arch=$(uname -i)
if [[ $arch == x86_64 ]];
then
    arch="x64"
fi
echo "Using CPU Arch ${arch}"

yum install -y wget && yum clean all
wget -q https://dotnetcli.azureedge.net/dotnet/Sdk/${DOTNET_VERSION}/dotnet-sdk-${DOTNET_VERSION}-linux-${arch}.tar.gz
mkdir -p /tmp/dotnet && tar zxf dotnet-sdk-${DOTNET_VERSION}-linux-${arch}.tar.gz -C /tmp/dotnet
