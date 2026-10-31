<?php
declare(strict_types=1);

namespace Hinasto\Core;

/** Text helpers. */
final class Text
{
    /**
     * Cuts a UTF-8 text to at most $max bytes without splitting a character. A byte-wise substr() can leave half of a
     * multibyte letter at the end, and the database (strict mode) refuses such a text.
     */
    public static function cut(string $s, int $max): string
    {
        if (strlen($s) <= $max) {
            return $s;
        }
        if (function_exists('mb_strcut')) {
            return mb_strcut($s, 0, $max, 'UTF-8');
        }
        $s = substr($s, 0, $max);
        $i = strlen($s) - 1;
        $tail = 0;  // continuation bytes at the end
        while ($i >= 0 && (ord($s[$i]) & 0xC0) === 0x80 && $tail < 3) {
            $i--;
            $tail++;
        }
        if ($i >= 0 && ord($s[$i]) >= 0xC0) {
            $lead = ord($s[$i]);
            $need = $lead >= 0xF0 ? 4 : ($lead >= 0xE0 ? 3 : 2);
            if ($tail + 1 < $need) {
                $s = substr($s, 0, $i);  // the character is cut: drop it whole
            }
        }
        return $s;
    }

    /** A value of a JSON body as text: only strings and numbers count, anything else (array, null) is ''. */
    public static function scalar(mixed $v): string
    {
        return is_string($v) || is_int($v) || is_float($v) ? (string)$v : '';
    }
}
