# MaastoPlugin

CloudCompare-plugin kehitysprojekti.

## Vaatimukset (Ubuntu)

- CMake >= 3.10
- GCC/G++
- Qt5 dev -paketit
- CloudCompare v2.13.2 lähdekoodi (erillisessä kansiossa)

```bash
sudo apt install cmake ninja-build qtbase5-dev qttools5-dev qttools5-dev-tools \
    libqt5svg5-dev libqt5opengl5-dev libqt5websockets5-dev libeigen3-dev libjsoncpp-dev
```

## CloudCompare-lähdekoodin kloonaus (kerran)

```bash
git clone --recursive --branch v2.13.2 \
    https://github.com/CloudCompare/CloudCompare.git \
    ~/CloudCompare-src
```

## Pluginin buildaus (Ubuntu)

```bash
# Kopioi plugin CC:n hakemistoon
cp -r /home/smo/MaastonPlugin/plugin ~/CloudCompare-src/plugins/private/qMaastoPlugin

# Tai käytä symlinkkiä (plugin-koodi pysyy vain MaastonPlugin-kansiossa):
ln -s /home/smo/MaastonPlugin/plugin ~/CloudCompare-src/plugins/private/qMaastoPlugin

# Buildaa
cmake -B ~/CloudCompare-src/build -S ~/CloudCompare-src \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DEIGEN_ROOT_DIR=/usr/include/eigen3 \
    -DJSON_ROOT_DIR=/usr/include/jsoncpp \
    -DPLUGIN_STANDARD_QMAASTOPLUGIN=ON \
    -DPLUGIN_IO_QCORE=ON

cmake --build ~/CloudCompare-src/build --parallel $(nproc)
cmake --install ~/CloudCompare-src/build --prefix ~/CloudCompare-install
```

## Testaus Ubuntussa

```bash
# Käynnistä buildartu CloudCompare
~/CloudCompare-install/bin/CloudCompare
```

Plugin näkyy **Plugins**-valikossa nimellä "MaastoPlugin".

## Windows-buildi (GitHub Actions)

Windows `.dll` buildataan automaattisesti GitHub Actionsissa joka pushin yhteydessä.

1. Pushaa muutokset GitHubiin
2. Avaa **Actions**-välilehti repossa
3. Lataa `MaastoPlugin-windows` artifakti
4. Kopioi `.dll` tiedosto: `C:\Program Files\CloudCompare\plugins\`
5. Käynnistä CloudCompare

## Rakenne

```
MaastonPlugin/
├── plugin/
│   ├── CMakeLists.txt
│   ├── info.json
│   ├── qMaastoPlugin.qrc
│   ├── images/icon.png
│   ├── include/
│   │   ├── qMaastoPlugin.h     - plugin-luokka
│   │   └── MaastoAction.h      - action-rajapinta
│   └── src/
│       ├── qMaastoPlugin.cpp   - plugin-toteutus
│       └── MaastoAction.cpp    - dialogi + hello world
└── .github/workflows/build.yml - CI/CD
```
