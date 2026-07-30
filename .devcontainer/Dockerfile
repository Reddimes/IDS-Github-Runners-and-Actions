FROM docker-test/Dockerfile

# Install ttyd
RUN apt-get update && apt-get install -y ttyd && apt-get clean && rm -rf /var/lib/apt/lists/*

# Set up the workspace
WORKDIR /app
