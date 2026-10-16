

install-dep:
	sudo apt install -y build-essential libpcap-dev wget
	cd /tmp && wget -nc https://github.com/appneta/tcpreplay/releases/download/v4.6.1/tcpreplay-4.6.1.tar.xz
	cd /tmp && tar -xf tcpreplay-4.6.1.tar.xz
	cd /tmp/tcpreplay-4.6.1 && ./configure && make -j$$(nproc)
	cd /tmp/tcpreplay-4.6.1 && sudo make install
	sudo mkdir -p /usr/local/include/tcpreplay/lib
	sudo cp /tmp/tcpreplay-4.6.1/lib/strlcpy.h /usr/local/include/tcpreplay/lib/strlcpy.h
	sudo chmod 644 /usr/local/include/tcpreplay/lib/strlcpy.h
	sudo ldconfig
	rm -rf /tmp/tcpreplay-4.6.1 /tmp/tcpreplay-4.6.1.tar.xz


comp:
	cmake -S . -B build
	cmake --build build

run:
	make comp
	./build/app

