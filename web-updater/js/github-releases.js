/**
 * github-releases.js
 *
 * Fetches firmware releases from the GitHub Releases API.
 * No API key required for public repositories (rate-limited to 60 req/hr).
 *
 * @license See COPYING.txt for license details.
 */

const GITHUB_API = 'https://api.github.com';

/*
 * CORS proxy for GitHub release asset downloads.
 *
 * The GitHub API (api.github.com) responds with proper CORS headers and is
 * fetched directly.  However, `browser_download_url` points to either
 * github.com or objects.githubusercontent.com — both of which redirect to a
 * CDN that does NOT send Access-Control-Allow-Origin headers.  Any direct
 * browser fetch of these URLs is blocked by CORS policy.
 *
 * Routing asset downloads through a CORS-capable proxy adds the required
 * headers so the browser can receive the binary data.
 *
 * corsfix.com is a free CORS proxy that works from GitHub Pages.
 * Format: CORS_PROXY + url  (raw URL appended directly, no encoding needed).
 */
const CORS_PROXY = 'https://proxy.corsfix.com/?';

/** Hostnames whose fetch responses are blocked by CORS policy. */
const CORS_BLOCKED_HOSTS = new Set([
    'objects.githubusercontent.com',
    'github.com',
]);

/**
 * Wrap a URL in the CORS proxy when it targets GitHub's download CDN.
 * API calls (api.github.com) are not proxied — they already have CORS headers.
 *
 * @param {string} url
 * @returns {string}
 */
function proxify(url) {
    try {
        const { hostname } = new URL(url);
        if (CORS_BLOCKED_HOSTS.has(hostname)) {
            return CORS_PROXY + url;
        }
    } catch (_) {
        /* malformed URL — pass through unchanged */
    }
    return url;
}

/**
 * Fetch available firmware releases from a GitHub repository.
 *
 * @param {string} owner - Repository owner (e.g. 'hapaxx11')
 * @param {string} repo  - Repository name  (e.g. 'M1')
 * @param {object} [options]
 * @param {boolean} [options.includePrerelease=true] - Include pre-releases
 * @param {number}  [options.perPage=20] - Max releases to fetch
 * @returns {Promise<Array>} Array of release objects with firmware assets
 */
export async function fetchReleases(owner, repo, options = {}) {
    const includePrerelease = options.includePrerelease !== false;
    const perPage = options.perPage || 20;

    const url = `${GITHUB_API}/repos/${owner}/${repo}/releases?per_page=${perPage}`;
    const resp = await fetch(url, {
        headers: { 'Accept': 'application/vnd.github.v3+json' },
    });

    if (!resp.ok) {
        throw new Error(`GitHub API error: ${resp.status} ${resp.statusText}`);
    }

    const releases = await resp.json();

    return releases
        .filter(r => includePrerelease || !r.prerelease)
        .map(r => {
            // Find the _wCRC.bin asset (the update image)
            const fwAsset = r.assets.find(a => a.name.endsWith('_wCRC.bin'));
            // Also find the plain .bin (no _wCRC suffix)
            const rawAsset = r.assets.find(a =>
                a.name.endsWith('.bin') && !a.name.endsWith('_wCRC.bin')
            );
            // Find SD card assets archive
            const sdAsset = r.assets.find(a => a.name === 'SD_Assets.zip');

            return {
                id: r.id,
                tagName: r.tag_name,
                name: r.name || r.tag_name,
                prerelease: r.prerelease,
                publishedAt: r.published_at,
                body: r.body,
                fwAsset: fwAsset ? {
                    name: fwAsset.name,
                    size: fwAsset.size,
                    downloadUrl: fwAsset.browser_download_url,
                } : null,
                rawAsset: rawAsset ? {
                    name: rawAsset.name,
                    size: rawAsset.size,
                    downloadUrl: rawAsset.browser_download_url,
                } : null,
                sdAsset: sdAsset ? {
                    name: sdAsset.name,
                    size: sdAsset.size,
                    downloadUrl: sdAsset.browser_download_url,
                } : null,
            };
        })
        .filter(r => r.fwAsset !== null);  // Only show releases with firmware
}

/**
 * Download a firmware binary from a release asset URL.
 *
 * @param {string}   url          - Direct download URL
 * @param {function} [onProgress] - Callback(loaded, total) for progress
 * @returns {Promise<Uint8Array>} Firmware binary data
 */
export async function downloadFirmware(url, onProgress = null) {
    const proxyUrl = proxify(url);
    const resp = await fetch(proxyUrl);
    if (!resp.ok) {
        throw new Error(`Download failed: ${resp.status} ${resp.statusText}`);
    }

    const contentLength = parseInt(resp.headers.get('Content-Length') || '0', 10);
    const reader = resp.body.getReader();
    const chunks = [];
    let loaded = 0;

    while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
        loaded += value.length;
        if (onProgress) {
            onProgress(loaded, contentLength);
        }
    }

    // Concatenate chunks into a single Uint8Array
    const result = new Uint8Array(loaded);
    let offset = 0;
    for (const chunk of chunks) {
        result.set(chunk, offset);
        offset += chunk.length;
    }

    return result;
}

/**
 * Download the SD card assets archive (SD_Assets.zip) from a release asset URL.
 *
 * @param {string}   url          - Direct download URL
 * @param {function} [onProgress] - Callback(loaded, total) for progress
 * @returns {Promise<Uint8Array>} ZIP archive data
 */
export async function downloadSdAssets(url, onProgress = null) {
    return downloadFirmware(url, onProgress);
}
