all: compile run

compile:
	gcc blazfs.c -o out/blazfs
	gcc blazfs_view.c -o out/blazfs_view

run:
	./out/blazfs file.txt FreeSans.sfn

view:
	./out/blazfs_view out/blazfs.fs