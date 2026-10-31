<?php
declare(strict_types=1);

use Hinasto\Controllers\SetupController;

// Used only while config/config.php does not exist yet.
return [
    ['GET',  '/',      [SetupController::class, 'home']],
    ['GET',  '/setup', [SetupController::class, 'form']],
    ['POST', '/setup', [SetupController::class, 'run']],
];
