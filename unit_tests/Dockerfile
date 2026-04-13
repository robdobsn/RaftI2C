FROM espressif/idf:v5.5.2
WORKDIR /project
# Install dependencies required for Node.js install
RUN apt-get update && apt-get install -y curl software-properties-common && \
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash - && \
    apt-get update && apt-get install -y nodejs g++
# Verify the installation of the specific Node.js version
RUN node -v && npm -v
# Configure Git to recognize /project as a safe directory
RUN git config --global --add safe.directory /project
