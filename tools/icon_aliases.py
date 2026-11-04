"""Map our canonical icon names (= Material Icon Theme file names) to other icon sets."""
import os

def canonical_names(root):
    d = os.path.join(root, "third_party", "material-icons")
    return sorted(f[:-4] for f in os.listdir(d) if f.endswith(".svg"))

CATPPUCCIN = {
    "react": "javascript-react", "react_ts": "typescript-react", "h": "c-header", "hpp": "cpp-header",
    "console": "bash", "settings": "config", "tune": "env", "document": "text", "table": "csv",
    "word": "ms-word", "powerpoint": "ms-powerpoint", "nodejs": "package-json", "python-misc": "python-config",
    "gemfile": "ruby-gem", "tsconfig": "typescript-config", "jsconfig": "javascript-config", "dll": "binary",
    "disc": "binary", "webassembly": "web-assembly", "tailwindcss": "tailwind", "visualstudio": "visual-studio",
    "browserlist": "browserslist", "objective-c": "c", "mdx": "markdown-mdx", "tex": "latex",
    "coffee": "coffeescript", "poetry": "poetry-lock", "turborepo": "turbo", "kubernetes": "helm",
    "github-actions-workflow": "workflow", "latexmk": "latex", "toml_light": "toml", "shader": "shader",
    "jupyter": "jupyter", "mathematica": "matlab", "sas": "r", "quarto": "markdown", "xaml": "xaml",
    "yarn": "yarn", "pnpm": "pnpm", "lerna": "lerna", "raml": "yaml", "solidity": "solidity",
}
SYMBOLS = {
    "javascript": "js", "typescript": "ts", "react_ts": "react-ts", "cpp": "cplus", "hpp": "h", "console": "shell",
    "powershell": "shell", "settings": "gear", "tune": "gear", "json": "brackets-yellow", "toml": "gear",
    "toml_light": "gear", "html": "code-orange", "css": "code-blue", "less": "code-blue", "zip": "compressed",
    "disc": "compressed", "dll": "exe", "lib": "exe", "certificate": "lock", "key": "lock", "go-mod": "go",
    "gemfile": "ruby", "nodejs": "node", "table": "csv", "word": "document", "powerpoint": "document",
    "log": "text", "changelog": "text", "todo": "text", "diff": "patch", "http": "globe", "makefile": "gear",
    "editorconfig": "gear", "jupyter": "notebook", "tailwindcss": "tailwind", "vim": "code-green",
    "python-misc": "python", "tsconfig": "tsconfig", "jsconfig": "gear", "objective-c": "c", "lisp": "code-purple",
    "scheme": "code-purple", "racket": "code-purple", "ocaml": "ocaml", "pascal": "code-gray", "ada": "code-gray",
    "cobol": "code-gray", "fortran": "fortran", "assembly": "code-gray", "verilog": "code-gray", "cuda": "cuda",
    "shader": "code-sky", "webassembly": "code-purple", "3d": "image", "blender": "image", "figma": "image",
    "arduino": "code-sky", "groovy": "gradle", "razor": "razor", "coffee": "coffeescript", "pug": "pug",
    "handlebars": "code-orange", "nunjucks": "nunjucks", "liquid": "liquid", "twig": "twig", "xaml": "xml",
    "raml": "yaml", "mdx": "mdx", "tex": "tex", "latexmk": "tex", "asciidoc": "document", "quarto": "markdown",
    "sas": "r", "mathematica": "code-red", "matlab": "code-red", "nginx": "gear", "bazel": "gear", "meson": "gear",
    "godot": "gdscript", "unity": "unity-scene", "kubernetes": "gear", "visualstudio": "visual-studio",
    "nuget": "visual-studio", "lerna": "npm", "turborepo": "turborepo", "rollup": "js", "esbuild": "js",
    "parcel": "js", "grunt": "js", "gulp": "gulp", "browserlist": "browserslist", "commitlint": "gear",
    "husky": "gear", "renovate": "renovate", "dependabot": "github", "github-actions-workflow": "github",
    "poetry": "python", "remix": "react", "gatsby": "gatsby", "storybook": "storybook", "playwright": "js-test",
    "cypress": "cypress", "jest": "jest", "vitest": "vitest", "nodemon": "nodemon", "deno": "deno", "bun": "bun",
    "nuxt": "nuxt", "next": "next", "astro": "astro", "angular": "angular", "taskfile": "taskfile", "gitlab": "gitlab",
    "mojo": "code-orange", "odin": "code-blue", "crystal": "crystal", "d": "code-red", "nim": "nim", "gleam": "gleam",
    "proto": "proto", "prisma": "prisma", "graphql": "graphql", "elixir": "elixir", "erlang": "erlang",
    "clojure": "clojure", "haskell": "haskell", "scala": "scala", "fsharp": "fsharp", "julia": "julia",
    "perl": "perl", "r": "r", "zig": "zig", "dart": "dart", "swift": "swift", "kotlin": "kotlin", "csharp": "csharp",
    "lua": "lua", "php": "php", "ruby": "ruby", "java": "java", "go": "go", "rust": "rust", "python": "python",
    "c": "c", "h": "h", "react": "react", "vue": "vue", "svelte": "svelte", "sass": "sass", "markdown": "markdown",
    "yaml": "yaml", "xml": "xml", "svg": "svg", "image": "image", "video": "video", "audio": "audio", "pdf": "pdf",
    "font": "font", "database": "database", "lock": "lock", "license": "license", "readme": "readme", "git": "git",
    "docker": "docker", "npm": "npm", "yarn": "yarn", "pnpm": "pnpm", "exe": "exe", "terraform": "terraform",
    "gradle": "gradle", "cmake": "cmake", "nix": "nix", "eslint": "eslint", "prettier": "prettier", "babel": "babel",
    "webpack": "webpack", "vite": "vite", "postcss": "postcss", "stylelint": "stylelint", "solidity": "solidity",
    "powerpoint_": "document", "document": "document", "android": "code-green", "vscode": "visual-studio",
    "android_": "code-green",
}

# Monokai palette by category (applied to the Symbols shapes)
MONOKAI = {"pink": "#FF6188", "orange": "#FC9867", "yellow": "#FFD866", "green": "#A9DC76", "blue": "#78DCE8",
           "purple": "#AB9DF2", "gray": "#939293", "white": "#FCFCFA"}
MONOKAI_CATEGORY = {
    "yellow": "javascript react json jsconfig nodejs npm yarn pnpm bun deno lerna rollup esbuild parcel grunt gulp".split(),
    "blue": "typescript react_ts tsconfig css less sass tailwindcss postcss go go-mod c h cpp hpp csharp dart docker kubernetes fsharp".split(),
    "orange": "html xml yaml toml toml_light rust java kotlin swift zip disc svg handlebars twig liquid nunjucks pug razor xaml raml".split(),
    "green": "console powershell vue nginx python python-misc poetry android git gitlab github-actions-workflow cuda shader arduino".split(),
    "pink": "ruby gemfile scala elixir erlang clojure haskell julia perl lua php r sas mathematica matlab angular svelte astro nuxt next remix gatsby graphql prisma".split(),
    "purple": "image video audio 3d blender figma font webassembly lisp scheme racket ocaml crystal nim gleam zig odin mojo d proto solidity".split(),
    "white": "markdown mdx document readme license changelog todo log text tex latexmk asciidoc quarto pdf word powerpoint table".split(),
}

# Folder icons (Material names) and the folder names that use them, from the Material Icon Theme's
# src/core/icons/folderIcons.ts (+ a few desktop folders). Matched case-insensitively, ignoring leading '.' / '_'.
FOLDERS = {
    'folder-src': ['src', 'srcs', 'source', 'sources', 'code'],
    'folder-dist': ['dist', 'out', 'output', 'outputs', 'build', 'builds', 'release', 'bin', 'distribution', 'built', 'compiled'],
    'folder-desktop': ['desktop', 'display'],
    'folder-images': ['images', 'image', 'imgs', 'img', 'icons', 'icon', 'icos', 'ico', 'figures', 'figure', 'figs', 'fig', 'screenshot', 'screenshots', 'screengrab', 'screengrabs', 'pic', 'pics', 'picture', 'pictures', 'photo', 'photos', 'photograph', 'photographs', 'texture', 'textures', 'wallpapers'],
    'folder-docs': ['doc', 'docs', 'document', 'documents', 'documentation', 'post', 'posts', 'article', 'articles', 'wiki', 'news', 'blog', 'knowledge', 'diary', 'note', 'notes'],
    'folder-download': ['downloads', 'download', 'downloader', 'downloaders'],
    'folder-audio': ['aud', 'auds', 'audio', 'audios', 'music', 'song', 'songs', 'sound', 'sounds', 'voice', 'voices', 'recordings', 'playlist', 'playlists'],
    'folder-video': ['vid', 'vids', 'video', 'videos', 'movie', 'movies', 'media'],
    'folder-node': ['node', 'nodejs', 'node_modules'],
    'folder-git': ['git', 'patches', 'githooks', 'submodules'],
    'folder-github': ['github'],
    'folder-vscode': ['vscode', 'vscode-test'],
    'folder-config': ['cfg', 'cfgs', 'conf', 'confs', 'config', 'configs', 'configuration', 'configurations', 'setting', 'settings', 'meta-inf', 'option', 'options', 'pref', 'prefs', 'preference', 'preferences', 'props', 'properties'],
    'folder-test': ['test', 'tests', 'testing', 'snapshots', 'spec', 'specs', 'testfiles'],
    'folder-public': ['public', 'www', 'wwwroot', 'web', 'website', 'websites', 'site', 'browser', 'browsers', 'proxy'],
    'folder-lib': ['lib', 'libs', 'library', 'libraries', 'vendor', 'vendors', 'thirdparty', 'third-party', 'lib64', 'external', 'externals', 'crates'],
    'folder-scripts': ['script', 'scripts', 'scripting', 'xtask'],
    'folder-components': ['components', 'widget', 'widgets', 'fragments'],
    'folder-include': ['inc', 'include', 'includes', 'partial', 'partials', 'inc64'],
    'folder-project': ['project', 'projects', 'proj', 'projs'],
    'folder-template': ['template', 'templates'],
    'folder-temp': ['temp', 'tmp', 'cached', 'cache'],
    'folder-target': ['target'],
    'folder-font': ['font', 'fonts', 'typeface', 'typefaces'],
    'folder-keys': ['key', 'keys', 'token', 'tokens', 'jwt', 'secret', 'secrets', 'ssh', 'gnupg'],
    'folder-trash': ['trash', 'trash-bin'],
    'folder-app': ['app', 'apps', 'application', 'applications'],
    'folder-database': ['db', 'data', 'database', 'databases', 'sql', 'storage'],
    'folder-resource': ['resource', 'resources', 'res', 'asset', 'assets', 'static', 'report', 'reports'],
    'folder-home': ['home', 'start', 'main', 'landing'],
    'folder-android': ['android'],
    'folder-archive': ['arc', 'arcs', 'archive', 'archives', 'archival'],
    'folder-backup': ['bkp', 'bkps', 'bak', 'baks', 'backup', 'backups', 'back-up', 'back-ups', 'history', 'histories'],
    'folder-log': ['log', 'logs', 'logging'],
    'folder-mail': ['mail', 'mails', 'email', 'emails', 'smtp', 'mailers'],
    'folder-private': ['private'],
    'folder-shared': ['shared', 'common'],
    'folder-secure': ['auth', 'authentication', 'secure', 'security', 'cert', 'certs', 'certificate', 'certificates', 'ssl', 'cipher', 'cypher', 'tls'],
    'folder-linux': ['linux', 'linuxbsd', 'unix', 'wsl', 'ubuntu', 'deb', 'debian', 'deepin', 'centos', 'popos', 'mint'],
    'folder-windows': ['windows', 'win', 'win32', 'windows11', 'windows10', 'windowsxp', 'windowsnt', 'win11', 'win10', 'winxp', 'winnt'],
    'folder-python': ['python', 'pycache', 'pytest_cache'],
    'folder-rust': ['rust', 'cargo'],
    'folder-docker': ['docker', 'dockerfiles', 'dockerhub'],
    'folder-tools': ['tools', 'toolkit', 'toolkits', 'toolbox', 'toolboxes', 'tooling', 'devtools', 'kit', 'kits'],
    'folder-css': ['css', 'stylesheet', 'stylesheets', 'style', 'styles'],
    'folder-api': ['api', 'apis', 'restapi'],
    'folder-server': ['server', 'servers', 'backend', 'backends', 'inventory', 'inventories', 'infrastructure', 'infra'],
    'folder-client': ['client', 'clients', 'frontend', 'frontends', 'pwa', 'spa'],
    'folder-mobile': ['mobile', 'mobiles', 'portable', 'portability', 'phone', 'phones'],
    'folder-admin': ['admin', 'admins', 'manager', 'managers', 'moderator', 'moderators'],
    'folder-plugin': ['plugin', 'plugins', 'mod', 'mods', 'modding', 'extension', 'extensions', 'addon', 'addons', 'addin', 'addins', 'module', 'modules'],
}

# Catppuccin folder icons are folder_<name>; these differ (anything missing falls back to _folder).
# folder_home / folder_trash in third_party/icons-catppuccin are file-explorer additions drawn in the set's style.
CATPPUCCIN_FOLDERS = {"folder": "_folder", "folder-test": "folder_tests", "folder-template": "folder_templates",
    "folder-font": "folder_fonts", "folder-keys": "folder_security", "folder-secure": "folder_security",
    "folder-resource": "folder_assets", "folder-plugin": "folder_plugins", "folder-rust": "folder_cargo",
    "folder-css": "folder_styles", "folder-mail": "folder_messages", "folder-tools": "folder_utils"}
# Symbols folder icons (anything missing falls back to the plain folder).
SYMBOLS_FOLDERS = {"folder": "folder", "folder-docs": "folder-documents", "folder-images": "folder-images",
    "folder-src": "folder-src", "folder-dist": "folder-build", "folder-node": "folder-node-modules",
    "folder-github": "folder-github", "folder-vscode": "folder-vscode", "folder-config": "folder-config",
    "folder-components": "folder-components", "folder-font": "folder-fonts", "folder-keys": "folder-lock",
    "folder-secure": "folder-lock", "folder-private": "folder-lock", "folder-app": "folder-app",
    "folder-database": "folder-database", "folder-shared": "folder-shared", "folder-target": "folder-target",
    "folder-resource": "folder-assets", "folder-mail": "folder-mail", "folder-android": "folder-android",
    "folder-docker": "folder-docker", "folder-tools": "folder-utils", "folder-plugin": "folder-modules",
    "folder-public": "folder-globe",
    "folder-home": "folder-home", "folder-trash": "folder-trash"}  # these two are file-explorer additions in the set's style

def catppuccin_folder(n): return CATPPUCCIN_FOLDERS.get(n, n.replace('folder-', 'folder_', 1))
def symbols_folder(n): return SYMBOLS_FOLDERS.get(n, 'folder')
