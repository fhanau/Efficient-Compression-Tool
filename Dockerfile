FROM alpine:3.18 as base
WORKDIR /app
RUN apk add --no-cache libgcc libstdc++

FROM base as build
RUN apk add --no-cache cmake g++ make nasm
COPY . .
RUN cmake -B build src
RUN make -C build -j

FROM base as runtime
COPY --from=build /app/build/ect .
CMD ["/app/ect"]
