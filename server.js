// server.js
// A small web server that runs our compiled seedinfo_full program
// and returns the results as JSON, so a website can call it.

const express = require("express");
const { execFile } = require("child_process");
const path = require("path");

const app = express();
const PORT = 3000;

// Path to the compiled C program (inside the cubiomes folder)
const SEEDINFO_PATH = path.join(__dirname, "cubiomes", "seedinfo_full");

app.get("/api/seed/:seed", (req, res) => {
  const seed = req.params.seed;

  // Basic validation: make sure it's a valid integer (allow negative seeds too)
  if (!/^-?\d+$/.test(seed)) {
    return res.status(400).json({ error: "Seed must be a valid integer." });
  }

  execFile(SEEDINFO_PATH, [seed], { timeout: 5000 }, (error, stdout, stderr) => {
    if (error) {
      console.error("Error running seedinfo_full:", error, stderr);
      return res.status(500).json({ error: "Failed to analyze seed." });
    }

    try {
      const data = JSON.parse(stdout);
      res.json(data);
    } catch (parseError) {
      console.error("Failed to parse output:", stdout);
      res.status(500).json({ error: "Failed to parse seed data." });
    }
  });
});

app.listen(PORT, () => {
  console.log(`Seed server running at http://localhost:${PORT}`);
});