<?php
declare(strict_types=1);

namespace Hinasto\Services;

use RuntimeException;

/** The database is ready but storage/config.php could not be written: carries the file's text for saving by hand. */
final class ConfigNotWritable extends RuntimeException
{
    public function __construct(public string $configPhp, public string $enrollKey)
    {
        parent::__construct('storage/ is not writable for the web server.');
    }
}
