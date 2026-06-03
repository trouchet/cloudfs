# Multi-stage build — final image is ~8 MB (scratch + static binary)
FROM golang:1.21-alpine AS builder
WORKDIR /build
COPY go.mod .
COPY main.go .
RUN CGO_ENABLED=0 GOOS=linux go build -ldflags="-s -w" -o cloudfs-agent .

FROM scratch
COPY --from=builder /build/cloudfs-agent /cloudfs-agent
EXPOSE 8765 8766
ENTRYPOINT ["/cloudfs-agent"]
