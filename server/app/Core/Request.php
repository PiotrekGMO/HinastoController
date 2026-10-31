<?php
declare(strict_types=1);

namespace Hinasto\Core;

final class Request
{
    private ?array $json = null;

    public function __construct(
        public string $method,
        public string $path,
        private array $query,
        private array $server,
        private array $post = [],
        public string $base = ''
    ) {
    }

    /**
     * $base is the URL prefix the app is served under: '' on a (sub)domain, '/hinasto' or '/hinasto/public'
     * when it lives in a sub directory. It is the part of the requested URL that is also in the path of
     * public/index.php (a rewrite such as .htaccess -> public/ hides a trailing /public).
     * $path is what is left for the router.
     */
    public static function fromGlobals(): self
    {
        $uri = parse_url((string)($_SERVER['REQUEST_URI'] ?? '/'), PHP_URL_PATH) ?: '/';
        $script = str_replace('\\', '/', (string)($_SERVER['SCRIPT_NAME'] ?? ''));
        $base = '';
        if (basename($script) === 'index.php') {
            $scriptDir = array_values(array_filter(explode('/', dirname($script)), 'strlen'));
            $uriParts = array_values(array_filter(explode('/', $uri), 'strlen'));
            $n = 0;
            while ($n < count($scriptDir) && $n < count($uriParts) && $scriptDir[$n] === $uriParts[$n]) {
                $n++;
            }
            $base = $n > 0 ? '/' . implode('/', array_slice($scriptDir, 0, $n)) : '';
        }
        if ($base !== '') {
            $uri = substr($uri, strlen($base)) ?: '/';
        }
        return new self(strtoupper((string)($_SERVER['REQUEST_METHOD'] ?? 'GET')), rtrim($uri, '/') ?: '/', $_GET, $_SERVER, $_POST, $base);
    }

    /** A field of a classic form post. */
    public function post(string $key, string $default = ''): string
    {
        $v = $this->post[$key] ?? $default;
        return is_string($v) ? $v : $default;
    }

    public function header(string $name): string
    {
        $key = 'HTTP_' . strtoupper(str_replace('-', '_', $name));
        return (string)($this->server[$key] ?? '');
    }

    public function query(string $key, string $default = ''): string
    {
        $v = $this->query[$key] ?? $default;
        return is_string($v) ? $v : $default;
    }

    public function ip(): string
    {
        $ip = (string)($this->server['REMOTE_ADDR'] ?? '');
        // Behind a reverse proxy or CDN every visitor has the proxy's address. With 'trusted_proxy' => true in
        // storage/config.php the LAST entry of X-Forwarded-For is used: it is the one the proxy added itself, the
        // entries before it come from the client and can be forged. Never switch it on without a proxy.
        if (Config::get('trusted_proxy', false) && ($xff = $this->header('X-Forwarded-For')) !== '') {
            $parts = array_map('trim', explode(',', $xff));
            $last = end($parts);
            if ($last !== false && filter_var($last, FILTER_VALIDATE_IP) !== false) {
                $ip = $last;
            }
        }
        return substr($ip, 0, 45);
    }

    /** Decoded JSON body (empty array if there is none). */
    public function json(int $maxBytes = 65536): array
    {
        if ($this->json === null) {
            $raw = file_get_contents('php://input', false, null, 0, $maxBytes + 1);
            if ($raw === false || $raw === '') {
                $this->json = [];
            } else {
                if (strlen($raw) > $maxBytes) {
                    throw new HttpException(413, 'body too large');
                }
                $data = json_decode($raw, true);
                if (!is_array($data)) {
                    throw new HttpException(400, 'invalid json');
                }
                $this->json = $data;
            }
        }
        return $this->json;
    }
}
