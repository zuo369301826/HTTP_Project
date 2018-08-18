http:http.c
	gcc $^ -o $@

.PHONY:clear

clear:
	rm http
