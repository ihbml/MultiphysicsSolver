FROM rust:1.82-slim AS build
WORKDIR /app
COPY Cargo.toml ./
COPY src ./src
RUN cargo build --release

FROM debian:bookworm-slim
RUN useradd -m mesher
COPY --from=build /app/target/release/fvm-sph-mesher /usr/local/bin/mesher
USER mesher
EXPOSE 8080
ENV MESHER_ADDR=0.0.0.0:8080
ENTRYPOINT ["/usr/local/bin/mesher"]
