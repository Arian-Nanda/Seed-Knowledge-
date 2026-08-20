#!/usr/bin/env python3
"""
wiki_fetch_history.py

Phase 3 of pulling wiki content into our knowledge base - History this
time (after mobs and advancements). Starting with the game's own overall
development history/timeline page, since per-item "History" sections
would mean fetching hundreds of individual pages - a much bigger
undertaking we'd want to scope separately if we want it later.
"""

import requests
import time
import os

API_URL = "https://minecraft.wiki/api.php"
OUTPUT_DIR = "wiki_raw/history"

HEADERS = {
    "User-Agent": "SmartSeed-Personal-Project/1.0 (personal Minecraft fan project, non-commercial; contact via GitHub Arian-Nanda)"
}


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


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    candidates = ["History of Minecraft", "Development of Minecraft", "History"]
    for title in candidates:
        print(f"Trying '{title}'...")
        content = get_page_wikitext(title)
        if content:
            print(f"  Found it - {len(content)} characters.")
            safe_name = title.replace(" ", "_") + ".txt"
            out_path = os.path.join(OUTPUT_DIR, safe_name)
            with open(out_path, "w", encoding="utf-8") as f:
                f.write(content)
            print(f"  Saved to {out_path}")
        else:
            print("  Not found.")
        time.sleep(0.5)

    print()
    print("Done - review what was found above before we design the parsing step.")


if __name__ == "__main__":
    main()
