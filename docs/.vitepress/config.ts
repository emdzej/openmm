import { defineConfig } from "vitepress";

export default defineConfig({
  title: "OpenMM",
  description:
    "A faithful reimplementation of Micro Machines (Codemasters, 1994, DOS) in C11 for the gasm runtime, using the data from your own copy of the game.",
  cleanUrls: true,
  lastUpdated: true,
  // The reverse-engineering notes (docs/re/, docs/formats/) stay in the repository and are not published.
  srcExclude: ["README.md", "AGENTS.md", "re/**", "formats/**"],
  sitemap: { hostname: "https://openmm.emdzej.pl" },

  head: [
    ["link", { rel: "icon", href: "/favicon.svg", type: "image/svg+xml" }],
    ["meta", { name: "theme-color", content: "#1b5f2e" }],
    ["meta", { property: "og:title", content: "OpenMM: Micro Machines, reimplemented" }],
    [
      "meta",
      {
        property: "og:description",
        content:
          "A faithful reimplementation of the DOS Micro Machines (1994) in C11, ported from the original program. Runs on gasm, natively and in the browser, with the data from your own floppy.",
      },
    ],
    ["meta", { property: "og:url", content: "https://openmm.emdzej.pl/" }],
  ],

  // The browser player is a static page in public/play/, not a Markdown page.
  ignoreDeadLinks: [/^\/play\//],

  themeConfig: {
    siteTitle: "OpenMM",
    logo: "/favicon.svg",

    nav: [
      { text: "Guide", link: "/guide/", activeMatch: "/guide/" },
      { text: "How-tos", link: "/howto/build", activeMatch: "/howto/" },
      // A static app in docs/public/play/: target _self makes it a full page load, not a VitePress route.
      { text: "Play", link: "/play/", target: "_self" },
      { text: "ENCORE", link: "https://encore.emdzej.pl" },
    ],

    sidebar: {
      "/guide/": [
        {
          text: "Guide",
          items: [
            { text: "Introduction", link: "/guide/" },
            { text: "Status", link: "/guide/status" },
            { text: "Theory of operation", link: "/guide/theory" },
            { text: "Game data", link: "/guide/game-data" },
          ],
        },
      ],
      "/howto/": [
        {
          text: "How-tos",
          items: [{ text: "Build from source", link: "/howto/build" }],
        },
      ],
    },

    socialLinks: [{ icon: "github", link: "https://github.com/emdzej/openmm" }],

    editLink: {
      pattern: "https://github.com/emdzej/openmm/edit/main/docs/:path",
      text: "Edit this page on GitHub",
    },

    outline: { level: [2, 3] },

    search: { provider: "local" },

    footer: {
      message:
        "Part of <a href=\"https://encore.emdzej.pl\">ENCORE</a> — Engines of Nostalgia, Clean Open REimplementations. OpenMM is released under the GPL-3.0. Micro Machines is © 1994 Codemasters. OpenMM contains no original code or assets and is not affiliated with Codemasters: it runs the game from your own copy. It runs on <a href=\"https://gasm.emdzej.pl\">gasm</a>. <a href=\"/credits\">Credits</a>",
    },
  },
});
