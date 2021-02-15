# Start from ubuntu image.
FROM ubuntu:latest

# Add the web dir.
RUN apt-get update && apt-get install -y --no-install-recommends nodejs npm

ADD web /web
WORKDIR web
RUN ls -l

# Install deps.
RUN npm install

# Expose web server port.
EXPOSE 7835

# Command to run on startup.
CMD node .
