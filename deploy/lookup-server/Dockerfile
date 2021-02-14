# Use ubuntu base image.
FROM ubuntu:latest

# Copy the binary.
ADD lookup-server /bin

# Expose listening port.
EXPOSE 12000

# Startup command runs our lookup server.
CMD lookup-server
