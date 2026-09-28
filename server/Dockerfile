# The Node relay in a small, locked-down container. See deploy/README.md.
FROM node:24-alpine
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --omit=dev --ignore-scripts && npm cache clean --force
COPY src/room.js ./src/room.js
COPY node-server.mjs ./
USER node
EXPOSE 8787
HEALTHCHECK --interval=60s --timeout=5s CMD wget -q -O /dev/null http://127.0.0.1:8787/ || exit 1
CMD ["node", "node-server.mjs", "--host", "0.0.0.0", "--port", "8787", "--trust-proxy"]
