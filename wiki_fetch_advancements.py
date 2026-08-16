#!/usr/bin/env python3
"""
wiki_fetch_advancements.py

Phase 2 of pulling wiki content into our knowledge base - Advancements
this time (after mob facts in Phase 1).
"""

import requests
import time
import os

API_URL = "https://minecraft.wiki/api.php"
OUTPUT_DIR = "wiki_raw/advancements"

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

    print("Checking Category:Advancements for individual pages...")
    try:
        titles = get_category_members("Advancements")
        print(f"  Found {len(titles)} pages. Sample: {titles[:10]}")
    except Exception as e:
        print(f"  ERROR: {e}")
        titles = []

    print()
    print("Checking the main 'Advancements' page directly...")
    try:
        main_page = get_page_wikitext("Advancements")
        if main_page:
            print(f"  Found it - {len(main_page)} characters.")
            with open(os.path.join(OUTPUT_DIR, "_main_page.txt"), "w", encoding="utf-8") as f:
                f.write(main_page)
            print(f"  Saved to {OUTPUT_DIR}/_main_page.txt for review.")
        else:
            print("  No page found with that exact title.")
    except Exception as e:
        print(f"  ERROR: {e}")

    if len(titles) > 3:
        print()
        print(f"Fetching {len(titles)} individual advancement pages...")
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
        print(f"Done fetching individual pages: {fetched} saved.")

    print()
    print("=== SUMMARY ===")
    print(f"Category page count: {len(titles)}")
    print(f"Main page fetched: {'yes' if os.path.exists(os.path.join(OUTPUT_DIR, '_main_page.txt')) else 'no'}")


if __name__ == "__main__":
    main()
