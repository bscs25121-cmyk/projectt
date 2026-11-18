FROM gcc:13
WORKDIR /app
COPY . .
RUN g++ -std=c++17 -O2 -pthread -o zixel main.cpp world.cpp http.cpp
ENV PORT=8080
EXPOSE 8080
CMD ["./zixel"]
