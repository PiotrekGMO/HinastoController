<?php
declare(strict_types=1);

namespace Hinasto\Services;

use Hinasto\Core\HttpException;
use Hinasto\Core\I18n;

/** Checks an uploaded ESP32-S2 image and reads its version ("HINASTO-FW:<n>") and language ("HINASTO-LANG:<code>") from the markers the firmware carries. */
final class FirmwareImage
{
    public const MAX_SIZE = 1310720;  // one OTA slot of the controller

    /** @return array{version:int, lang:string, size:int, md5:string} */
    public static function inspect(string $path): array
    {
        $size = (int)filesize($path);
        if ($size < 100000 || $size > self::MAX_SIZE) {
            throw new HttpException(400, t('err.fw_size', ['size' => $size]));
        }
        $bin = (string)file_get_contents($path);
        if ($bin[0] !== "\xE9" || substr($bin, 12, 2) !== "\x02\x00") {  // image magic, chip id 2 = ESP32-S2
            throw new HttpException(400, t('err.fw_chip'));
        }
        if (!preg_match('/HINASTO-FW:(\d{1,5})\x00/', $bin, $m)) {
            throw new HttpException(400, t('err.fw_marker'));
        }
        // "HINASTO-LANG:<code>": builds from before languages existed carry none and are Polish
        $lang = preg_match('/HINASTO-LANG:([A-Z]{2})\x00/', $bin, $l) ? strtolower($l[1]) : 'pl';
        if (!I18n::valid($lang)) {
            throw new HttpException(400, t('err.fw_lang', ['lang' => $l[1]]));
        }
        return ['version' => (int)$m[1], 'lang' => $lang, 'size' => $size, 'md5' => md5($bin)];
    }
}