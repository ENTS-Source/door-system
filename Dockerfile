# ---- Stage 0 ----
# Builds tool binaries
FROM golang:1.26-alpine AS builder

# Install build dependencies
RUN apk add --no-cache git musl-dev dos2unix

WORKDIR /opt
COPY ./control-app /opt

# Run build steps
RUN GOBIN=$PWD/bin go install -v ./cmd/...

# ---- Stage 1 ----
# Final runtime stage.
FROM alpine

ENV DC_HTTP_BIND=0.0.0.0:8080

COPY --from=builder /opt/bin/controller /usr/local/bin/

CMD /usr/local/bin/controller
EXPOSE 8080