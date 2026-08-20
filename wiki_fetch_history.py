#!/usr/bin/env python3
import requests
import os
import time

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

    pages = {
        "Java Edition version history": "Java_Edition_version_history.txt",
        "Bedrock Edition version history": "Bedrock_Edition_version_history.txt",
    }

    for title, filename in pages.items():
        print(f"Fetching '{title}'...")
        content = get_page_wikitext(title)
        if not content:
            print("  Not found.")
            continue
        print(f"  Found it - {len(content)} characters.")
        out_path = os.path.join(OUTPUT_DIR, filename)
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"  Saved to {out_path}")
        time.sleep(0.5)


if __name__ == "__main__":
    main()
