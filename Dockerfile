# Base image: Node.js on Debian ("bookworm"), which includes apt-get so we
# can install gcc/build tools - NOT using the "-alpine" or "-slim" variants,
# since those lack full build tools by default.
FROM node:20-bookworm

# Install the C compiler and related build tools needed to compile our
# seedinfo_full/seedcombo/etc programs.
RUN apt-get update && apt-get install -y build-essential && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Install Node dependencies first (small, simple - just express + dotenv)
COPY package.json package-lock.json ./
RUN npm install

# Copy the rest of the project (source code, cubiomes folder, public files, etc.)
COPY . .

# Compile every C program fresh, directly in this build environment - safer
# than reusing binaries built somewhere else (like the Codespace), since
# this guarantees compatibility with wherever it's actually running.
RUN cd cubiomes && \
    gcc -O2 seedinfo_full.c libcubiomes.a -fwrapv -lm -lpthread -o seedinfo_full && \
    gcc -O2 seedcombo.c libcubiomes.a -fwrapv -lm -lpthread -o seedcombo && \
    gcc -O2 seedinfo_full_bedrock.c Bfinders.c libcubiomes.a -fwrapv -lm -lpthread -o seedinfo_full_bedrock && \
    gcc -O2 seedcombo_bedrock.c Bfinders.c libcubiomes.a -fwrapv -lm -lpthread -o seedcombo_bedrock && \
    gcc -O2 seedstronghold.c libcubiomes.a -fwrapv -lm -lpthread -o seedstronghold && \
    gcc -O2 lootpredict.c libcubiomes.a -fwrapv -lm -lpthread -o lootpredict

# The port our Node server listens on (matches server.js's PORT constant)
EXPOSE 3000

CMD ["node", "server.js"]