// server.js
// A small web server that runs our compiled seedinfo_full/seedcombo programs
// and returns the results as JSON, so a website can call it.
// Supports both Java Edition (with version selection) and Bedrock Edition
// (always the latest version, since Bedrock players can't opt into old ones).
// Also includes an /api/ask endpoint - a general Minecraft chat assistant
// powered by Groq's cloud API plus a Minecraft knowledge base, with visual
// crafting-grid diagrams (generated deterministically, no AI involved) when
// the answer involves a recipe.

require("dotenv").config();
const express = require("express");
const { execFile } = require("child_process");
const path = require("path");

const app = express();
app.use(express.json());
const PORT = process.env.PORT || 3000;

// Serve the frontend files (index.html, etc.) from the "public" folder
app.use(express.static(path.join(__dirname, "public")));

// Paths to the compiled C programs (inside the cubiomes folder)
const SEEDINFO_JAVA_PATH = path.join(__dirname, "cubiomes", "seedinfo_full");
const SEEDINFO_BEDROCK_PATH = path.join(__dirname, "cubiomes", "seedinfo_full_bedrock");
const SEEDCOMBO_JAVA_PATH = path.join(__dirname, "cubiomes", "seedcombo");
const SEEDSTRONGHOLD_PATH = path.join(__dirname, "cubiomes", "seedstronghold");
const SEEDCOMBO_BEDROCK_PATH = path.join(__dirname, "cubiomes", "seedcombo_bedrock");

// Valid Minecraft versions this project supports (Java only - Bedrock always
// uses the latest version internally, since Bedrock players can't choose).
const VALID_VERSIONS = [
  "1.0","1.1","1.2","1.3","1.4","1.5","1.6","1.7","1.8","1.9","1.10",
  "1.11","1.12","1.13","1.14","1.15","1.16","1.17","1.18","1.19","1.20","1.21",
];

function isBedrock(platform) {
  return platform === "bedrock";
}

// Runs the seed-info C program (Java or Bedrock) and returns a Promise that
// resolves with the parsed JSON.
function runSeedInfo(platform, seed, version, x, z) {
  return new Promise((resolve, reject) => {
    const bedrock = isBedrock(platform);
    const binPath = bedrock ? SEEDINFO_BEDROCK_PATH : SEEDINFO_JAVA_PATH;

    const args = bedrock ? [seed] : [seed, version];
    if (x !== undefined && z !== undefined) {
      args.push(x, z);
    }

    execFile(binPath, args, { timeout: 5000 }, (error, stdout, stderr) => {
      if (error) {
        console.error("Error running seedinfo:", error, stderr);
        return reject(new Error("Failed to analyze seed."));
      }
      try {
        resolve(JSON.parse(stdout));
      } catch (parseError) {
        console.error("Failed to parse output:", stdout);
        reject(new Error("Failed to parse seed data."));
      }
    });
  });
}

// Runs the combo-finder C program (Java or Bedrock) and returns a Promise
// with parsed JSON. The C program handles the escalating tiered search
// internally (20k -> 40k -> 60k -> 80k -> 100k blocks), only scanning new
// area at each step, and reports which tier it settled on.
function runSeedCombo(platform, seed, version, x, z, typesCsv, countsCsv) {
  return new Promise((resolve, reject) => {
    const bedrock = isBedrock(platform);
    const binPath = bedrock ? SEEDCOMBO_BEDROCK_PATH : SEEDCOMBO_JAVA_PATH;

    const args = bedrock
      ? [seed, String(x), String(z), typesCsv]
      : [seed, version, String(x), String(z), typesCsv];
    if (countsCsv) args.push(countsCsv);

    execFile(binPath, args, { timeout: 60000 }, (error, stdout, stderr) => {
      if (error) {
        console.error("Error running seedcombo:", error, stderr);
        return reject(new Error(stderr.trim() || "Failed to search structure combinations."));
      }
      try {
        resolve(JSON.parse(stdout));
      } catch (parseError) {
        console.error("Failed to parse combo output:", stdout);
        reject(new Error("Failed to parse combination data."));
      }
    });
  });
}

// Validates the platform + version combination. Returns an error message
// string if invalid, or null if everything checks out.
function validatePlatformAndVersion(platform, version) {
  if (platform !== "java" && platform !== "bedrock") {
    return "Platform must be 'java' or 'bedrock'.";
  }
  if (platform === "java" && !VALID_VERSIONS.includes(version)) {
    return "Unsupported version.";
  }
  return null;
}

app.get("/api/combo/:seed", async (req, res) => {
  const seed = req.params.seed;
  const types = req.query.types;
  const counts = req.query.counts; // optional - comma-separated, parallel to types
  const x = req.query.x !== undefined ? req.query.x : "0";
  const z = req.query.z !== undefined ? req.query.z : "0";
  const platform = req.query.platform || "java";
  const version = req.query.version || "1.21";

  if (!/^-?\d+$/.test(seed)) {
    return res.status(400).json({ error: "Seed must be a valid integer." });
  }
  const platformError = validatePlatformAndVersion(platform, version);
  if (platformError) {
    return res.status(400).json({ error: platformError });
  }
  if (!/^-?\d+$/.test(x) || !/^-?\d+$/.test(z)) {
    return res.status(400).json({ error: "x and z must both be valid integers." });
  }
  if (!types || typeof types !== "string" || types.trim() === "") {
    return res.status(400).json({ error: "At least one structure type is required." });
  }
  if (counts !== undefined && !/^\d+(,\d+)*$/.test(counts)) {
    return res.status(400).json({ error: "Counts must be a comma-separated list of positive integers." });
  }

  try {
    const data = await runSeedCombo(platform, seed, version, x, z, types, counts);
    res.json(data);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Stronghold is deliberately NOT part of the main seed search - its
// biome-based checking is significantly slower than every other structure,
// so it only runs when a user specifically asks for it. Java Edition only -
// Bedrock's stronghold algorithm is not implemented in the library we use.
function runSeedStronghold(seed, version, x, z) {
  return new Promise((resolve, reject) => {
    const args = [seed, version];
    if (x !== undefined && z !== undefined) {
      args.push(x, z);
    }

    execFile(SEEDSTRONGHOLD_PATH, args, { timeout: 15000 }, (error, stdout, stderr) => {
      if (error) {
        console.error("Error running seedstronghold:", error, stderr);
        return reject(new Error("Failed to find stronghold."));
      }
      try {
        resolve(JSON.parse(stdout));
      } catch (parseError) {
        console.error("Failed to parse output:", stdout);
        reject(new Error("Failed to parse stronghold data."));
      }
    });
  });
}

app.get("/api/stronghold/:seed", async (req, res) => {
  const seed = req.params.seed;
  const platform = req.query.platform || "java";
  const version = req.query.version || "1.21";

  if (!/^-?\d+$/.test(seed)) {
    return res.status(400).json({ error: "Seed must be a valid integer." });
  }
  if (platform === "bedrock") {
    return res.status(400).json({ error: "Stronghold search isn't available for Bedrock yet." });
  }
  if (!VALID_VERSIONS.includes(version)) {
    return res.status(400).json({ error: "Unsupported version." });
  }

  let x, z;
  if (req.query.x !== undefined || req.query.z !== undefined) {
    x = req.query.x;
    z = req.query.z;
    if (!/^-?\d+$/.test(x) || !/^-?\d+$/.test(z)) {
      return res.status(400).json({ error: "x and z must both be valid integers." });
    }
  }

  try {
    const data = await runSeedStronghold(seed, version, x, z);
    res.json(data);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

app.get("/api/seed/:seed", async (req, res) => {
  const seed = req.params.seed;
  const platform = req.query.platform || "java";
  const version = req.query.version || "1.21";

  if (!/^-?\d+$/.test(seed)) {
    return res.status(400).json({ error: "Seed must be a valid integer." });
  }
  const platformError = validatePlatformAndVersion(platform, version);
  if (platformError) {
    return res.status(400).json({ error: platformError });
  }

  let x, z;
  if (req.query.x !== undefined || req.query.z !== undefined) {
    x = req.query.x;
    z = req.query.z;
    if (!/^-?\d+$/.test(x) || !/^-?\d+$/.test(z)) {
      return res.status(400).json({ error: "x and z must both be valid integers." });
    }
  }

  try {
    const data = await runSeedInfo(platform, seed, version, x, z);
    res.json(data);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// General Minecraft chat assistant, powered by Groq's cloud API. Can
// have a normal back-and-forth conversation, draws on a built-in Minecraft
// knowledge base for accuracy, and - if the person has searched a seed -
// also has access to that seed's real structure data as extra context.
const { retrieveContext } = require("./knowledge/retrieveKnowledge.js");
const { MINECRAFT_KNOWLEDGE } = require("./knowledge/minecraftKnowledge.js");

// All 1,105 block facts, in the exact order they appear in the knowledge
// base - used for the deterministic daily rotation below.
const ALL_BLOCK_FACTS = MINECRAFT_KNOWLEDGE.filter((f) => f.includes("(block):"));

// Parses a "{Name} (block): hardness H; blast resistance B; drops: ...;
// harvestable with: ...." fact into structured data. Tested against all
// 1,105 real block facts with zero parse failures.
function parseBlockFact(fact) {
  const headerMatch = fact.match(/^(.+?) \(block\): (.+)\.$/);
  if (!headerMatch) return null;
  const [, name, rest] = headerMatch;
  const parts = rest.split("; ");

  const result = { name };
  for (const part of parts) {
    if (part.startsWith("hardness ")) result.hardness = parseFloat(part.replace("hardness ", ""));
    else if (part.startsWith("blast resistance ")) result.blastResistance = parseFloat(part.replace("blast resistance ", ""));
    else if (part.startsWith("drops: ")) result.drops = part.replace("drops: ", "").split(", ");
    else if (part.startsWith("harvestable with: ")) result.harvestTools = part.replace("harvestable with: ", "").split(", ");
    else if (part === "cannot be mined normally") result.cannotBeMined = true;
  }
  return result;
}

// Picks today's block deterministically - same block for everyone on a
// given day, cycling through all 1,105 blocks over about 3 years. No AI
// call involved at all, so this is instant, free, and always 100% accurate
// (it's just our already-verified structured data, nothing generated).
function getBlockOfTheDay() {
  const daysSinceEpoch = Math.floor(Date.now() / 86400000);
  const index = daysSinceEpoch % ALL_BLOCK_FACTS.length;
  const fact = ALL_BLOCK_FACTS[index];
  return parseBlockFact(fact);
}

app.get("/api/block-of-the-day", (req, res) => {
  const block = getBlockOfTheDay();
  const today = new Date().toISOString().slice(0, 10);
  res.json({ date: today, block });
});

// Parses a "Recipe for X (makes N): crafted in a grid - ..." or
// "...shapeless, combine ..." fact back into a structured grid, so the
// frontend can render an actual visual crafting diagram. Returns null if
// the fact isn't a recipe fact (or doesn't match the expected format).
function parseRecipeFact(fact) {
  if (!fact || typeof fact !== "string") return null;
  const headerMatch = fact.match(/^Recipe for (.+?) \(makes (\d+)\): (.+)$/);
  if (!headerMatch) return null;
  const [, name, count, rest] = headerMatch;

  if (rest.startsWith("shapeless, combine ")) {
    const ingredients = rest
      .replace("shapeless, combine ", "")
      .replace(/\.$/, "")
      .split(", ");
    return { name, count: Number(count), shapeless: ingredients };
  }

  if (rest.startsWith("crafted in a grid - ")) {
    const rowsPart = rest.replace("crafted in a grid - ", "").replace(/\.$/, "");
    const rows = rowsPart.split("; ").map((rowStr) => {
      const cellsStr = rowStr.replace(/^row \d+: /, "");
      return cellsStr.split(" | ").map((c) => (c === "empty" ? null : c));
    });
    return { name, count: Number(count), shape: rows };
  }

  return null;
}

// Pads a recipe's shape to always be a full 3x3 grid (adding empty rows/
// columns as needed), so the visual diagram is never a confusing partial
// grid - e.g. a hoe recipe that only uses 2 columns still renders as a
// clear 3x3 with the extra column shown empty, not cut off.
function padShapeTo3x3(shape) {
  const padded = shape.map((row) => {
    const newRow = row.slice(0, 3);
    while (newRow.length < 3) newRow.push(null);
    return newRow;
  });
  while (padded.length < 3) padded.push([null, null, null]);
  return padded;
}

const ROW_LABELS = ["top", "middle", "bottom"];
const COL_LABELS = ["left", "middle", "right"];

// Generates a guaranteed-accurate, step-by-step crafting explanation
// directly from the structured recipe data - no LLM involved. This exists
// because testing showed the local model would sometimes describe recipes
// incorrectly (even flatly contradicting the data it was given), so for
// recipe questions we use this deterministic text instead.
function generateRecipeAnswerText(visual) {
  if (visual.shapeless) {
    return `To craft ${visual.name} (makes ${visual.count}): combine these in a crafting grid, in any arrangement - ${visual.shapeless.join(", ")}.`;
  }

  const padded = padShapeTo3x3(visual.shape);
  const steps = [];
  for (let r = 0; r < 3; r++) {
    for (let c = 0; c < 3; c++) {
      const item = padded[r][c];
      if (item) {
        steps.push(`place ${item} in the ${ROW_LABELS[r]}-${COL_LABELS[c]} slot`);
      }
    }
  }
  const stepText = steps.length > 0
    ? steps.map((s, i) => `${i + 1}. ${s.charAt(0).toUpperCase() + s.slice(1)}.`).join(" ")
    : "";
  return `To craft ${visual.name} (makes ${visual.count}): ${stepText}`;
}

// Uses Groq's cloud API instead of a local model - no persistent background
// process needed (which was the main obstacle to hosting this publicly),
// and a much more capable model (70B parameters vs the local 3B one).
const GROQ_MODEL = "llama-3.3-70b-versatile";

async function askGroq(messages) {
  if (!process.env.GROQ_API_KEY) {
    throw new Error("Server is missing a Groq API key. Set GROQ_API_KEY in your .env file.");
  }

  const res = await fetch("https://api.groq.com/openai/v1/chat/completions", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "Authorization": `Bearer ${process.env.GROQ_API_KEY}`,
    },
    body: JSON.stringify({
      model: GROQ_MODEL,
      messages,
      temperature: 0.3,
      max_tokens: 400,
    }),
  });

  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`Groq API returned an error: ${text || res.statusText}`);
  }

  const data = await res.json();
  return data.choices?.[0]?.message?.content || "";
}

app.post("/api/ask", async (req, res) => {
  const { seed, question, x, z } = req.body;
  const platform = req.body.platform || "java";
  const version = req.body.version || "1.21";
  const history = Array.isArray(req.body.history) ? req.body.history : [];

  if (!question || typeof question !== "string" || question.trim() === "") {
    return res.status(400).json({ error: "A question is required." });
  }

  const SEED_RELATED_WORDS = [
    "seed", "structure", "coordinate", "spawn", "nearby", "distance", "away",
    "village", "monument", "mansion", "temple", "pyramid", "outpost", "portal",
    "fortress", "bastion", "city", "mineshaft", "stronghold", "treasure",
    "well", "geode", "gateway", "shipwreck", "ruin", "igloo", "hut",
  ];
  const questionLower = question.toLowerCase();
  const seemsSeedRelated = SEED_RELATED_WORDS.some((w) => questionLower.includes(w));

  let seedData = null;
  if (seemsSeedRelated && seed && /^-?\d+$/.test(String(seed))) {
    const platformError = validatePlatformAndVersion(platform, version);
    if (!platformError) {
      try {
        seedData = await runSeedInfo(platform, String(seed), version, x, z);
      } catch (err) {
        console.error("Seed lookup failed for /api/ask:", err.message);
      }
    }
  }

  let seedContextBlock = "";
  if (seedData) {
    const foundEntries = Object.entries(seedData.structures).filter(([, pos]) => pos);
    const pairwiseDistances = [];
    for (let i = 0; i < foundEntries.length; i++) {
      for (let j = i + 1; j < foundEntries.length; j++) {
        const [nameA, posA] = foundEntries[i];
        const [nameB, posB] = foundEntries[j];
        const dx = posA.x - posB.x;
        const dz = posA.z - posB.z;
        const distance = Math.round(Math.sqrt(dx * dx + dz * dz));
        pairwiseDistances.push({ a: nameA, b: nameB, distance });
      }
    }
    const dataForAI = { ...seedData, pairwiseDistances };
    seedContextBlock = `\n\nThe person has searched this specific seed. Use this real data if their question relates to it:\n${JSON.stringify(dataForAI, null, 2)}`;
  }

  const knowledgeChunks = retrieveContext(question, 3);
  const knowledgeBlock = knowledgeChunks.length > 0
    ? `\n\nRelevant Minecraft reference facts:\n${knowledgeChunks.map((c) => `- ${c}`).join("\n")}`
    : "";

  const craftingVisual = parseRecipeFact(knowledgeChunks[0] || "");
  if (craftingVisual && craftingVisual.shape) {
    craftingVisual.shape = padShapeTo3x3(craftingVisual.shape);
  }

  if (craftingVisual) {
    return res.json({
      answer: generateRecipeAnswerText(craftingVisual),
      seedData,
      craftingVisual,
    });
  }

  const systemPrompt = `You are a friendly, knowledgeable Minecraft assistant, similar to Mojang's Merlin. Have a natural conversation, and answer Minecraft questions accurately.

Rules:
- If reference facts are provided below, prefer them over your own memory for specifics like recipes and mechanics - your own memory of exact numbers is often wrong.
- Minecraft speeds and distances are always measured in blocks (or blocks per second) - never use frames per second or any other unit for in-game speed or distance.
- If seed structure data is provided, use it only when the question is about that seed - never invent coordinates.
- If you're not confident about something, say so rather than guessing.
- Keep answers conversational and not overly long unless the person asks for detail.${knowledgeBlock}${seedContextBlock}`;

  const messages = [
    { role: "system", content: systemPrompt },
    ...history,
    { role: "user", content: question },
  ];

  try {
    const answerText = await askGroq(messages);
    res.json({ answer: answerText, seedData, craftingVisual });
  } catch (err) {
    console.error("Failed to reach Groq:", err.message);
    res.status(500).json({
      error: err.message.includes("GROQ_API_KEY")
        ? err.message
        : "Could not reach the AI service. Please try again in a moment.",
    });
  }
});

app.listen(PORT, () => {
  console.log(`Seed server running at http://localhost:${PORT}`);
});