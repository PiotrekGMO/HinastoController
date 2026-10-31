<?php
declare(strict_types=1);

namespace Hinasto\Core;

use RuntimeException;

/** Thrown anywhere to end the request with an HTTP error; the front controller turns it into a response. */
final class HttpException extends RuntimeException
{
    public function __construct(public int $status, string $message)
    {
        parent::__construct($message);
    }
}
