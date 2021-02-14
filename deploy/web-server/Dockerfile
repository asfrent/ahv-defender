# Start from node image.
FROM node:latest

# Add the web dir.
ADD web /web
WORKDIR web

# Install deps.
RUN npm install

# Expose web server port.
EXPOSE 7835

# Command to run on startup.
CMD node .
