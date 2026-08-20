#!/usr/bin/env python3
"""
wiki_fetch_biomes.py

Phase 5 of pulling wiki content into our knowledge base - Biomes this
time (after mobs, advancements, game history, and mob history).
"""

import requests
import time
import os

API_URL = "https://minecraft.wiki/api.php"
OUTPUT_DIR = "wiki_raw/biomes"

HEADERS = {
    "User-Agent": "SmartSeed-Personal-Project/1.0 (personal Minecraft fan project, non-commercial; contact via GitHub Arian-Nanda)"
}


def get_category_members(category, cmtype="page", limit=500):
    members = []
    cmcontinue = None
    while True:
        params = {
            "action": "query",
            "list": "categorymembers",
            "cmtitle": f"Category:{category}",
            "cmlimit": limit,
            "cmtype": cmtype,
            "format": "json",
        }
        if cmcontinue:
            params["cmcontinue"] = cmcontinue
        resp = requests.get(API_URL, params=params, headers=HEADERS, timeout=15)
        resp.raise_for_status()
        data = resp.json()
        batch = data.get("query", {}).get("categorymembers", [])
        members.extend(p["title"] for p in batch)
        if "continue" in data:
            cmcontinue = data["continue"]["cmcontinue"]
            time.sleep(0.5)
        else:
            break
    return members


def get_page_wikitext(title):
    params = {
        "action": "query",
        "titles": title,
        "prop": "revisions",
        "rvprop": "content",
        "rvslots": "main",
        "format": "json",
    }
    resp = requests.get(API_URL, params=params, headers=HEADERS, timeout=15)
    resp.raise_for_status()
    data = resp.json()
    pages = data.get("query", {}).get("pages", {})
    for page_id, page in pages.items():
        if page_id == "-1":
            return None
        revisions = page.get("revisions", [])
        if revisions:
            return revisions[0]["slots"]["main"]["*"]
    return None


def safe_filename(title):
    return title.replace("/", "_").replace(" ", "_").replace(":", "_") + ".txt"


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    print("Checking Category:Biomes for individual pages...")
    try:
        titles = get_category_members("Biomes")
        print(f"  Found {len(titles)} pages. Sample: {titles[:10]}")
    except Exception as e:
        print(f"  ERROR: {e}")
        titles = []

    if len(titles) == 0:
        print("Trying 'Biome' (singular) directly as a fallback...")
        content = get_page_wikitext("Biome")
        if content:
            print(f"  Found it - {len(content)} characters.")
            with open(os.path.join(OUTPUT_DIR, "_Biome_main.txt"), "w", encoding="utf-8") as f:
                f.write(content)
        return

    fetched = 0
    for i, title in enumerate(titles):
        out_path = os.path.join(OUTPUT_DIR, safe_filename(title))
        if os.path.exists(out_path):
            continue
        try:
            content = get_page_wikitext(title)
        except Exception as e:
            print(f"  [{i+1}/{len(titles)}] ERROR on '{title}': {e}")
            continue
        if content is None:
            continue
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(content)
        fetched += 1
        print(f"  [{i+1}/{len(titles)}] Saved: {title} ({len(content)} chars)")
        time.sleep(0.5)

    print()
    print(f"Done. Fetched {fetched} pages.")


if __name__ == "__main__":
    main()
