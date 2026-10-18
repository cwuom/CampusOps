# Lists CC0 sounds on Freesound for a query (most downloaded first), with duration, rating, downloads and tags.
# Usage: python freesound_search.py "grenade explosion" [pages]
import re, sys, html, urllib.parse, urllib.request

UA = {"User-Agent": "Mozilla/5.0"}


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=30).read().decode("utf-8", "replace")


def search(query, pages=1):
    out = []
    for page in range(1, pages + 1):
        q = urllib.parse.urlencode({"q": query, "f": 'license:"Creative Commons 0"', "s": "Downloads (most first)", "page": page})
        h = get("https://freesound.org/search/?" + q)
        for m in re.finditer(r'href="/people/([^/]+)/sounds/(\d+)/"[^>]*title="([^"]*)"', h):
            item = (m.group(2), m.group(1), html.unescape(m.group(3)))
            if item not in out:
                out.append(item)
    return out


def details(user, sid):
    h = get(f"https://freesound.org/people/{user}/sounds/{sid}/")
    cc0 = "publicdomain/zero" in h
    dur = re.search(r'Duration</dt>\s*<dd[^>]*>\s*([^<]+)', h)
    dl = re.search(r'([\d,]+)\s*downloads', h)
    rating = re.search(r'data-rating="([\d.]+)"|avg[_-]rating[^\d]*([\d.]+)', h)
    tags = re.findall(r'href="/browse/tags/([^/"]+)/?"', h)
    desc = re.search(r'<div id="soundDescriptionSection"[^>]*>(.*?)</div>', h, re.S) or re.search(r'class="sound-description[^"]*"[^>]*>(.*?)</div>', h, re.S)
    preview = re.search(r'https://cdn\.freesound\.org/previews/\d+/\d+_\d+-hq\.ogg', h)
    d = re.sub(r"<[^>]+>", " ", desc.group(1)) if desc else ""
    return {
        "cc0": cc0,
        "duration": dur.group(1).strip() if dur else "?",
        "downloads": dl.group(1) if dl else "?",
        "tags": ",".join(dict.fromkeys(tags))[:120],
        "desc": " ".join(html.unescape(d).split())[:220],
        "preview": preview.group(0) if preview else "",
    }


if __name__ == "__main__":
    query = sys.argv[1]
    pages = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 14
    for sid, user, name in search(query, pages)[:limit]:
        try:
            d = details(user, sid)
        except Exception as e:  # noqa: BLE001
            print(f"{sid} {user} {name}: error {e}")
            continue
        print(f"{sid} | {user} | {name} | {d['duration']} | dl {d['downloads']} | cc0 {d['cc0']}\n    tags: {d['tags']}\n    {d['desc']}")
