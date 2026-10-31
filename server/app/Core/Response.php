<?php
declare(strict_types=1);

namespace Hinasto\Core;

final class Response
{
    public function __construct(
        private string $body,
        private int $status = 200,
        private array $headers = []
    ) {
    }

    public static function json(array $data, int $status = 200): self
    {
        return new self(
            json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_PARTIAL_OUTPUT_ON_ERROR),
            $status,
            ['Content-Type' => 'application/json; charset=utf-8', 'Cache-Control' => 'no-store']
        );
    }

    public static function html(string $html, int $status = 200): self
    {
        return new self($html, $status, [
            'Content-Type' => 'text/html; charset=utf-8',
            'Cache-Control' => 'no-store',
            'X-Frame-Options' => 'DENY',
            'Referrer-Policy' => 'no-referrer',
        ]);
    }

    private ?string $file = null;

    /** A binary file sent as it is (firmware images). */
    public static function file(string $path, string $downloadName): self
    {
        $r = new self('', 200, [
            'Content-Type' => 'application/octet-stream',
            'Content-Length' => (string)filesize($path),
            'Content-Disposition' => 'attachment; filename="' . $downloadName . '"',
            'Cache-Control' => 'no-store',
        ]);
        $r->file = $path;
        return $r;
    }

    public function send(): void
    {
        http_response_code($this->status);
        foreach ($this->headers as $name => $value) {
            header("$name: $value");
        }
        if ($this->file !== null) {
            @ini_set('zlib.output_compression', '0');
            if (function_exists('apache_setenv')) {
                @apache_setenv('no-gzip', '1');
            }
            header('X-Accel-Buffering: no');  // nginx in front: do not buffer (and re-chunk) a big file
            while (ob_get_level() > 0) {
                ob_end_clean();
            }
            set_time_limit(0);
            readfile($this->file);
            return;
        }
        echo $this->body;
    }
}
