alias INSTALL=''

INSTALL error () { echo "ERROR [$(basename $0)]: $*" > /dev/stderr ; exit 1 ; }
INSTALL PYTHON_VERSION=3.10
INSTALL PYTHON_VENV=${HOME:-/tmp}/.gridlabd
INSTALL PYTHON_EXEC=$PYTHON_VENV/bin/python$PYTHON_VERSION
INSTALL PYTHON_CONFIG=$PYTHON_VENV/bin/python${PYTHON_VERSION}-config

# check for root access
if [ "$(whoami)" != "root" ]; then
    error "you must run setup as root or use sudo"
fi

# prepare apt for installations
INSTALL DEBIAN_FRONTEND=noninteractive
INSTALL apt-get update -y

# setup requires python version if not already installed
if ! python$PYTHON_VERSION --version 1>/dev/null 2>&1 ; then
    echo "Installing python${PYTHON_VERSION}..."
    INSTALL test -f /etc/localtime || ln -s /usr/share/zoneinfo/UTC /etc/localtime
    INSTALL apt-get install software-properties-common -y
    INSTALL add-apt-repository ppa:deadsnakes/ppa -y
    INSTALL apt-get install python$PYTHON_VERSION -y
    python$PYTHON_VERSION --version || error "python$PYTHON_VERSION installation failed"
fi
INSTALL apt-get install python$PYTHON_VERSION-venv -y
python$PYTHON_VERSION -m pip install setuptools

# create python venv for setup if not already done
if [ ! -x "$PYTHON_EXEC" ] ; then
    INSTALL python$PYTHON_VERSION -m venv $PYTHON_VENV
    test -x "$PYTHON_EXEC" || error "python venv creation failed"
fi

# activate the build environment for python
INSTALL . $PYTHON_VENV/bin/activate

# upgrade pip if needed
if ! "$PYTHON_EXEC" -m pip --version 1>/dev/null 2>&1 ; then
    INSTALL curl --retry 5 -fsL https://bootstrap.pypa.io/get-pip.py | python$PYTHON_VERSION
    INSTALL "$PYTHON_EXEC" -m pip --version || error "pip installation failed"
fi

# check gdal
if ! gdal-config --version 1>/dev/null 2>&1 ; then
    apt-get install libgdal-dev -y
    gdal-config --version || error "libgdal-dev installation failed"
fi

# install python-config
if ! "python$PYTHON_VERSION-config" --prefix 1>/dev/null 2>&1 ; then
    INSTALL apt-get install python$PYTHON_VERSION-dev -y
    python$PYTHON_VERSION-config --prefix || error "python$PYTHON_VERSION-config installation failed"
fi
INSTALL "$PYTHON_EXEC" -m pip install --upgrade pip || error "pip update failed"

# install required libraries
INSTALL apt-get install build-essential zlib1g-dev libcurl4-gnutls-dev libncurses5-dev liblzma-dev libbz2-dev libgdbm-dev libnss3-dev libssl-dev libreadline-dev libffi-dev libmysqlclient-dev mysql-server -y

# install required tools
INSTALL apt-get install git unzip libtool libtool-bin mdbtools g++ cmake flex bison subversion util-linux xz-utils wget -y

# update library paths
INSTALL ldconfig

# symlink mysql files
ln -s /usr/include/mysql /usr/local/include/mysql
ln -s /usr/lib/aarch64-linux-gnu/libmysqlclient.a /usr/local/lib/libmysqlclient.a

# install autoconf 2.72 as required
if [ "$(autoconf --version | head -n 1 | cut -f4 -d' ')" != "2.72" ] ; then
    (cd /tmp ; curl --retry 5 -fsL https://ftp.gnu.org/gnu/autoconf/autoconf-2.72.tar.gz | tar xz )
    (cd /tmp/autoconf-2.72 ; ./configure ; make ; make install)
    test "$(autoconf --version | head -n 1 | cut -f4 -d' ')" = "2.72" || error "autoconf installation failed"
fi

# make sure python3.10 is available through /usr/local/bin
ln -sf $(which python3.10) /usr/local/bin/python3
ln -sf $(which python3.10-config) /usr/local/bin/python3-config
