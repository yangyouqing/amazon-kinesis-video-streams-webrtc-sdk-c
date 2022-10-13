#!/bin/sh

set -x
basepath=$(cd `dirname $0`; pwd)
echo $basepath

if [ ! -d "mp4v2" ]; then
	git clone -b Release-MP4v2-3.0.0.0 https://github.com/TechSmith/mp4v2.git
	cd mp4v2
	git checkout Release-MP4v2-3.0.0.0
	git apply $basepath/MP4v2-3.0.0.0.patch
	./configure 
	make -j8
	#sudo make install
	 
	cp include/mp4v2 $basepath/../../open-source/include/ -rf
	cp .libs/libmp4v2.* $basepath/../../open-source/lib/ -f
	cd -
fi

#cp mp4v2/include/mp4v2 $basepath/../../open-source/include/ -rf
#cp mp4v2/.libs/libmp4v2.* $basepath/../../open-source/lib/ -f
