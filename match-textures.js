// match-textures.js
// Matches every block in our knowledge base to a real Faithful texture
// file. Many blocks reuse another block's texture entirely (stairs/slabs/
// walls reuse their base material, wall-mounted variants reuse the
// standing version, carpets reuse wool, infested blocks reuse their
// normal counterpart, etc.) - discovered by examining real unmatched
// results across two prior rounds against the actual texture files.
//
// Some transformations need to be applied more than once in sequence
// (e.g. "Smooth Sandstone Stairs" -> "Smooth Sandstone" -> the regular
// sandstone texture), so this does an iterative/breadth-first search
// instead of a single-pass transform.

const fs = require("fs");
const path = require("path");

const MAPPING_PATH = "./displayname_to_internalname.json";
const TEXTURE_SOURCE_DIR = "./faithful-textures/assets/minecraft/textures/block";
const OUTPUT_DIR = "./public/block-textures";

const nameMap = JSON.parse(fs.readFileSync(MAPPING_PATH, "utf8"));

const { MINECRAFT_KNOWLEDGE } = require("./knowledge/minecraftKnowledge.js");
const blockFacts = MINECRAFT_KNOWLEDGE.filter((f) => f.includes("(block):"));
const displayNames = blockFacts.map((f) => f.match(/^(.+?) \(block\)/)[1]);

if (!fs.existsSync(OUTPUT_DIR)) fs.mkdirSync(OUTPUT_DIR, { recursive: true });

const FALLBACK_SUFFIXES = ["", "_top", "_side", "_front", "_all", "_still", "_0"];

function fileExists(name) {
  for (const suffix of FALLBACK_SUFFIXES) {
    const candidate = path.join(TEXTURE_SOURCE_DIR, `${name}${suffix}.png`);
    if (fs.existsSync(candidate)) return candidate;
  }
  return null;
}

const WOOD_PREFIXES = [
  "oak", "spruce", "birch", "jungle", "acacia", "cherry", "dark_oak",
  "pale_oak", "mangrove", "bamboo", "crimson", "warped",
];

const CROP_STAGE_SUFFIXES = ["_stage7", "_stage3", "_stage2"];

// Each rule takes a name and returns an array of alternate candidates to
// try. Rules are applied repeatedly (breadth-first) so chained
// transformations resolve correctly.
const TRANSFORM_RULES = [
  // Stairs/slabs/walls reuse their base material's texture directly.
  (name) => name.endsWith("_stairs") ? [name.replace(/_stairs$/, "")] : [],
  (name) => name.endsWith("_slab") ? [name.replace(/_slab$/, "")] : [],
  (name) => (name.endsWith("_wall") && !name.startsWith("wall_")) ? [name.replace(/_wall$/, "")] : [],

  // Singular "brick"/"_brick" vs the real plural "bricks"/"_bricks" texture
  // filename. Fixed a bug where this only worked with a prefix before -
  // the bare word "brick" (no underscore) needed its own check too.
  (name) => (name === "brick" || name.endsWith("_brick")) ? [`${name}s`] : [],
  // Same pluralization issue affects "tile" (e.g. Deepslate Tiles).
  (name) => (name === "tile" || name.endsWith("_tile")) ? [`${name}s`] : [],

  // Wall-mounted variants reuse the standing/plain version's texture.
  (name) => name === "wall_torch" ? ["torch"] : [],
  (name) => name.endsWith("_wall_torch") ? [name.replace(/_wall_torch$/, "_torch")] : [],
  (name) => name.endsWith("_wall_sign") ? [name.replace(/_wall_sign$/, "_sign")] : [],
  (name) => name.endsWith("_wall_hanging_sign") ? [name.replace(/_wall_hanging_sign$/, "_hanging_sign")] : [],
  (name) => name.endsWith("_wall_banner") ? [name.replace(/_wall_banner$/, "_banner")] : [],
  (name) => name.endsWith("_wall_skull") ? [name.replace(/_wall_skull$/, "_skull")] : [],
  (name) => name.endsWith("_wall_head") ? [name.replace(/_wall_head$/, "_head")] : [],
  (name) => name.endsWith("_wall_fan") ? [name.replace(/_wall_fan$/, "_fan")] : [],

  // All-bark "Wood" blocks reuse the corresponding Log texture.
  (name) => name.endsWith("_wood") ? [name.replace(/_wood$/, "_log")] : [],

  // Waxed copper looks identical to its non-waxed equivalent.
  (name) => name.startsWith("waxed_") ? [name.replace(/^waxed_/, "")] : [],

  // Potted plants reuse the plain plant's own texture.
  (name) => name.startsWith("potted_") ? [name.replace(/^potted_/, "")] : [],

  // Infested blocks look identical to their normal (non-infested) counterpart.
  (name) => name.startsWith("infested_") ? [name.replace(/^infested_/, "")] : [],

  // Carpets reuse the equivalent color's Wool texture.
  (name) => name.endsWith("_carpet") ? [name.replace(/_carpet$/, "_wool")] : [],

  // Cauldron variants (water/lava/powder snow) reuse the plain cauldron texture.
  (name) => /^(water|lava|powder_snow)_cauldron$/.test(name) ? ["cauldron"] : [],

  // Wooden fences/gates/plates/buttons/doors/trapdoors/stairs/slabs reuse
  // that wood type's PLANKS texture specifically.
  (name) => {
    const shapes = ["_fence_gate", "_fence", "_pressure_plate", "_button", "_door", "_trapdoor", "_stairs", "_slab"];
    for (const shape of shapes) {
      if (name.endsWith(shape)) {
        const prefix = name.replace(shape, "");
        if (WOOD_PREFIXES.includes(prefix)) return [`${prefix}_planks`];
      }
    }
    return [];
  },

  // A fence not made of wood (e.g. Nether Brick Fence) reuses its base
  // material's texture directly - the pluralization rule above will then
  // handle "nether_brick" -> "nether_bricks" if needed.
  (name) => name.endsWith("_fence") && !WOOD_PREFIXES.includes(name.replace(/_fence$/, ""))
    ? [name.replace(/_fence$/, "")] : [],

  // "Smooth" stone variants reuse the top texture of their non-smooth base.
  (name) => name === "smooth_sandstone" ? ["sandstone_top"] : [],
  (name) => name === "smooth_red_sandstone" ? ["red_sandstone_top"] : [],
  (name) => name === "smooth_quartz" ? ["quartz_block_top", "quartz_top"] : [],

  // Crops use a growth-stage suffix rather than a bare name - showing the
  // fully-grown stage is the most visually representative choice.
  (name) => {
    const crops = ["wheat", "carrots", "potatoes", "beetroots", "nether_wart", "cocoa", "torchflower_crop", "pitcher_crop", "sweet_berry_bush"];
    return crops.includes(name) ? CROP_STAGE_SUFFIXES.map((s) => `${name}${s}`) : [];
  },

  // Nether "Hyphae" (all-bark nether wood) reuses the Stem texture, same
  // relationship as Wood -> Log for overworld trees.
  (name) => name.endsWith("_hyphae") ? [name.replace(/_hyphae$/, "_stem")] : [],

  // Any button/pressure plate not made of wood (stone, blackstone, etc.)
  // reuses its base material's texture directly.
  (name) => {
    if (name.endsWith("_button")) return [name.replace(/_button$/, "")];
    if (name.endsWith("_pressure_plate")) return [name.replace(/_pressure_plate$/, "")];
    return [];
  },

  // A handful of specific irregular names that don't follow a general
  // pattern - the shape variant's prefix doesn't exactly match its base
  // material's actual texture name.
  (name) => name === "petrified_oak" ? ["oak_planks"] : [],
  (name) => name === "purpur" ? ["purpur_block"] : [],
  (name) => name === "quartz" ? ["quartz_block_side", "quartz_block_top"] : [],
  (name) => name === "magma_block" ? ["magma"] : [],
  (name) => name === "moss_carpet" ? ["moss_block"] : [],
  (name) => name === "bamboo" ? ["bamboo_stalk"] : [],

  // Cake-with-candle composites reuse the plain cake texture as the
  // closest reasonable stand-in (loses the candle detail, but shows the
  // right general block).
  (name) => name.endsWith("_candle_cake") || name === "candle_cake" ? ["cake"] : [],

  // Weighted pressure plates: try the block-form textures too, in case
  // the plate-specific ones don't exist either.
  (name) => name === "light_weighted_pressure_plate" ? ["gold_pressure_plate", "gold_block"] : [],
  (name) => name === "heavy_weighted_pressure_plate" ? ["iron_pressure_plate", "iron_block"] : [],
];

// Breadth-first search: try the name directly, then apply every rule to
// generate new candidates, then apply rules again to THOSE candidates
// (up to a few rounds), so chained transformations resolve correctly.
function findTexture(startName) {
  let frontier = [startName];
  const seen = new Set(frontier);

  for (let round = 0; round < 3; round++) {
    for (const candidate of frontier) {
      const found = fileExists(candidate);
      if (found) return found;
    }
    const nextFrontier = [];
    for (const candidate of frontier) {
      for (const rule of TRANSFORM_RULES) {
        for (const next of rule(candidate)) {
          if (!seen.has(next)) {
            seen.add(next);
            nextFrontier.push(next);
          }
        }
      }
    }
    if (nextFrontier.length === 0) break;
    frontier = nextFrontier;
  }
  return null;
}

let matched = 0;
let unmatched = [];

for (const displayName of displayNames) {
  const internalName = nameMap[displayName];
  if (!internalName) {
    unmatched.push(`${displayName} (no internal name mapping)`);
    continue;
  }

  const found = findTexture(internalName);

  if (found) {
    fs.copyFileSync(found, path.join(OUTPUT_DIR, `${internalName}.png`));
    matched++;
  } else {
    unmatched.push(`${displayName} (${internalName}.png not found, no fallback matched)`);
  }
}

console.log(`Matched: ${matched} / ${displayNames.length}`);
console.log(`Unmatched: ${unmatched.length}`);
if (unmatched.length > 0) {
  fs.writeFileSync("./unmatched-blocks.txt", unmatched.join("\n"));
  console.log("Full unmatched list written to unmatched-blocks.txt");
}