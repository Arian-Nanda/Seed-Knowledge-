#!/usr/bin/env python3
"""
wiki_fetch_advancements.py

Phase 2 of pulling wiki content into our knowledge base - Advancements.
The main page is "Advancement" (singular) - "Advancements" (plural)
turned out to be just a redirect to it, discovered during the first run.
"""

import requests
import time
import os
import re

API_URL = "https://minecraft.wiki/api.php"
OUTPUT_DIR = "wiki_raw/advancements"

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

    print("Fetching the 'Advancement' (singular) page directly...")
    content = get_page_wikitext("Advancement")

    if content is None:
        print("Still couldn't find it - something else is going on, let's check together.")
        return

    print(f"Found it - {len(content)} characters.")
    out_path = os.path.join(OUTPUT_DIR, "Advancement_main.txt")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"Saved to {out_path}")

    headers = re.findall(r"^==+ *(.+?) *==+$", content, re.MULTILINE)
    print()
    print(f"Found {len(headers)} section headers on this page. First 20:")
    for h in headers[:20]:
        print(" ", h)


if __name__ == "__main__":
    main()
