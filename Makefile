#makes a movie of a ball bouncing
all:    
	g++ -I ../ffmpeg/include/ bouncer.cc -L ../ffmpeg/lib `pkg-config --cflags --libs libavutil libavformat libavcodec libswscale`

clean:
	rm -rf *.o a.out
	rm -rf *.utah a.out

movie:
	ffmpeg -f image2 -r 30 -i frame%03d.utah movie.mp4
