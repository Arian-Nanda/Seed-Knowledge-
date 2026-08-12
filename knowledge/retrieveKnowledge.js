// retrieveKnowledge.js
// Finds the most relevant Minecraft knowledge chunks for a given question.
// Uses frequency-weighted keyword matching: words that appear in fewer
// chunks count more toward the score than common words that appear
// everywhere, so specific terms (e.g. "wither") aren't drowned out by
// generic ones. Also gives a boost to recipe facts specifically when the
// question sounds like a crafting question ("how do I make/craft/build X") -
// without this, generic terms like "diamond" or "pickaxe" appear in hundreds
// of facts (every block that needs one to mine, etc.) and can bury the
// actual recipe under unrelated matches.

const { MINECRAFT_KNOWLEDGE } = require("./minecraftKnowledge.js");

const STOP_WORDS = new Set([
  "the","a","an","is","are","how","do","does","did","i","to","make","made",
  "what","of","for","in","on","and","with","work","works","can","you","my",
  "me","it","this","that","be","has","have","was","were","will","would",
  "should","could","there","their","they","them","about","get","got",
]);

// Very basic stemming: strips common suffixes so related word forms match
// each other (e.g. "minecarts" -> "minecart", "enchantment" and
// "enchanting" both -> "enchant"). Crude, not a real linguistic stemmer,
// but a meaningful improvement over exact-word-only matching.
function normalize(word) {
  if (word.length > 4 && word.endsWith("es")) word = word.slice(0, -2);
  else if (word.length > 3 && word.endsWith("s") && !word.endsWith("ss")) word = word.slice(0, -1);

  if (word.length > 7 && word.endsWith("ment")) return word.slice(0, -4);
  if (word.length > 6 && word.endsWith("ing")) return word.slice(0, -3);

  return word;
}

// A recipe fact whose own output name closely matches the query wins
// outright (see retrieveContext below) - this no longer depends on
// specific trigger words like "make"/"craft" being present, since someone
// typing just an item name (e.g. "enchantment table") should still get
// that item's recipe.

const RECIPE_PREFIX = "Recipe for ";
function extractRecipeName(chunk) {
  if (!chunk.startsWith(RECIPE_PREFIX)) return null;
  const match = chunk.match(/^Recipe for (.+?) \(makes/);
  return match ? match[1] : null;
}
const recipeNameWordSets = MINECRAFT_KNOWLEDGE.map((chunk) => {
  const name = extractRecipeName(chunk);
  if (!name) return null;
  return new Set(name.toLowerCase().split(/\W+/).filter((w) => w.length > 2).map(normalize));
});

// Precompute how many chunks each word appears in (document frequency).
const docFreq = {};
const chunkWordSets = MINECRAFT_KNOWLEDGE.map((chunk) => {
  const words = new Set(
    chunk.toLowerCase().split(/\W+/).filter((w) => w.length > 2).map(normalize)
  );
  for (const w of words) docFreq[w] = (docFreq[w] || 0) + 1;
  return words;
});

// Generic words about the ACT of crafting/making something - these are
// excluded specifically from the recipe-name-matching logic below, because
// one of them ("craft") is coincidentally also part of a real item name
// ("Crafting Table"/"Crafter"). Without this exclusion, any "how do I
// craft X" question would create a false tie with Crafting Table itself,
// regardless of what X actually is - a real bug found in testing.
const GENERIC_ACTION_WORDS = new Set(["make", "made", "craft", "crafting", "build", "recipe"]);

function retrieveContext(userMessage, topN = 3) {
  const rawLower = userMessage.toLowerCase();

  const words = rawLower
    .split(/\W+/)
    .filter((w) => w.length > 2 && !STOP_WORDS.has(w))
    .map(normalize);

  if (words.length === 0) return [];

  const recipeMatchWords = words.filter((w) => !GENERIC_ACTION_WORDS.has(w));
  const hasRecipeMatchWords = recipeMatchWords.length > 0;

  // Step 1: check for a confident recipe-name match first. If the query
  // closely matches a specific recipe's own output name (e.g. "enchantment
  // table" clearly matching "Enchanting Table"), that recipe should win
  // outright - not just compete on similar footing with block-property
  // facts or hand-written facts, which was causing inconsistent results.
  //
  // Requires BOTH:
  // - The recipe's name is FULLY covered by the query (ratio === 1) - a
  //   partial match isn't enough (e.g. "nether portal" partially matching
  //   "Nether Bricks" via just the word "nether" should NOT win).
  // - The match explains the MAJORITY of the query itself (not just the
  //   recipe name) - otherwise a single generic word like "redstone" would
  //   trigger "Redstone Comparator" even when someone asks "how does
  //   redstone work", or "minecart" would trigger its own recipe even when
  //   asked "how fast are minecarts" (a totally different question).
  // Among multiple full-name matches, the one covering more of the query
  // wins (e.g. "redstone torch" should pick "Redstone Torch" over "Torch").
  let bestRecipeIdx = -1;
  let bestQueryCoverage = 0;
  if (hasRecipeMatchWords) {
    for (let i = 0; i < MINECRAFT_KNOWLEDGE.length; i++) {
      const nameWords = recipeNameWordSets[i];
      if (!nameWords || nameWords.size === 0) continue;

      // If this recipe's own name genuinely contains a generic action word
      // (e.g. "Crafting Table" contains "craft"), use the full,
      // unfiltered query words for this specific comparison - otherwise
      // a query like "crafting table" would have "craft" excluded and
      // could never fully match "Crafting Table" itself. For every other
      // recipe, keep using the filtered words so "craft"/"make" used as a
      // generic verb doesn't cause false ties with unrelated items.
      const nameNeedsGenericWords = [...nameWords].some((w) => GENERIC_ACTION_WORDS.has(w));
      const matchWordsForThis = nameNeedsGenericWords ? words : recipeMatchWords;

      const matchedNameWords = matchWordsForThis.filter((w) => nameWords.has(w)).length;
      const nameMatchRatio = matchedNameWords / nameWords.size;
      if (nameMatchRatio < 1) continue; // require full name coverage

      const queryCoverage = matchedNameWords / matchWordsForThis.length;
      if (queryCoverage > 0.5 && queryCoverage > bestQueryCoverage) {
        bestQueryCoverage = queryCoverage;
        bestRecipeIdx = i;
      }
    }
  }

  const results = [];
  if (bestRecipeIdx >= 0) {
    results.push(MINECRAFT_KNOWLEDGE[bestRecipeIdx]);
  }

  // Step 2: fill remaining slots with normal keyword-overlap scoring
  // (skipping whatever we already picked above, to avoid duplicates).
  const LOCATION_INTENT_WORDS = ["where", "find", "generate", "generates", "located", "spawn", "get"];
  const hasLocationIntent = LOCATION_INTENT_WORDS.some((w) => rawLower.includes(w));

  // A stronger, more specific signal than plain "where/find" - these words
  // unambiguously point at natural world-generation (not structure loot),
  // e.g. "what height does X generate" or "what biomes have X".
  const NATURAL_GEN_INTENT_WORDS = ["height", "biome", "biomes", "naturally", "y level", "ylevel", "underground", "ore"];
  const hasNaturalGenIntent = NATURAL_GEN_INTENT_WORDS.some((w) => rawLower.includes(w));

  // Signals someone wants to know what a STRUCTURE is physically built
  // from (walls/floors), not what's in its treasure chests.
  const ARCHITECTURE_INTENT_WORDS = ["built", "build", "made of", "made from", "make up", "makes up", "material", "materials", "constructed", "walls"];
  const hasArchitectureIntent = ARCHITECTURE_INTENT_WORDS.some((w) => rawLower.includes(w));

  // Signals a genuine mob combat/stats question - these should win over
  // older, less-detailed facts or unrelated items that happen to share
  // the mob's name (e.g. "Piglin Spawn Egg") in a tied match.
  const MOB_STAT_INTENT_WORDS = ["damage", "health", "hp", "attack", "speed", "hostile", "hostility", "hit points", "fast", "move", "moving"];
  const hasMobStatIntent = MOB_STAT_INTENT_WORDS.some((w) => rawLower.includes(w));

  const scored = MINECRAFT_KNOWLEDGE.map((chunk, i) => {
    if (i === bestRecipeIdx && results.length > 0) return { chunk, score: -1 };
    let score = 0;
    const chunkLowerStart = chunk.toLowerCase();
    for (const w of words) {
      if (chunkWordSets[i].has(w)) {
        score += 1 / (docFreq[w] || 1);
        // A fact that literally starts with the matched term (our
        // hand-written facts follow a "Term: description..." pattern) is
        // usually more centrally relevant than one that just mentions the
        // term in passing - e.g. "Redstone: acts like..." should beat
        // "Iron tools: ...can mine diamond, redstone...".
        if (chunkLowerStart.startsWith(w)) {
          score += 0.5;
        }
      }
    }
    // "Where can I find X" questions specifically want structure-loot
    // facts (which structures contain X), not just any fact mentioning X -
    // without this, ties with older facts (like plain item stats) were
    // winning simply due to appearing earlier in the knowledge base.
    if (hasLocationIntent && chunk.includes("(structure loot)") && score > 0) {
      score += 1;
    }
    // A more specific boost for genuinely natural-generation-flavored
    // questions (height/biome/etc.), so these win over structure loot
    // when the question is clearly about world generation, not chests.
    if (hasNaturalGenIntent && chunk.includes("(natural generation)") && score > 0) {
      score += 1.5;
    }
    // Similarly, "what is X built from" should win over structure loot
    // facts for the same structure.
    if (hasArchitectureIntent && chunk.includes("(structure architecture)") && score > 0) {
      score += 1.5;
    }
    // "How much damage/health does X have" should win over older,
    // less-detailed facts and unrelated items (like spawn eggs) that
    // happen to share the same starting name.
    if (hasMobStatIntent && chunk.includes("(mob)") && score > 0) {
      score += 1.5;
    }
    return { chunk, score };
  });

  const remaining = scored
    .filter((s) => s.score > 0)
    .sort((a, b) => b.score - a.score)
    .slice(0, topN - results.length)
    .map((s) => s.chunk);

  return [...results, ...remaining];
}

module.exports = { retrieveContext };