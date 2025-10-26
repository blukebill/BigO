#parser-c/dockerfile
FROM alpine:3.20 AS build
RUN apk add --no-cache build-base cmake git jansson-dev icu-dev
WORKDIR /src
COPY . .
# initialize tree-sitter submods if present
RUN git submodule update --init --recursive || true
RUN cmake -S . -B build && cmake --build build --config Release

FROM alpine:3.20
RUN apk add --no-cache jansson
WORKDIR /app
COPY --from=build /src/build/parser /app/parser
EXPOSE 7001
CMD ["/app/parser", "--port", "7001"]
