<?php

declare(strict_types=1);

use PhpCsFixer\Config;
use PhpCsFixer\Finder;
use PhpCsFixer\Runner\Parallel\ParallelConfigFactory;

$finder = (new Finder())
    ->in([
        __DIR__ . '/lib',
        __DIR__ . '/tests/unit_tests',
    ])
    ->name('*.php')
    ->ignoreDotFiles(true)
    ->ignoreVCS(true);

return (new Config())
    ->setRules([
      '@PHP71Migration' => true,
      '@PSR12' => true,
    ])
    ->setRiskyAllowed(true)
    ->setFinder($finder)
    ->setIndent('    ')
    ->setParallelConfig(ParallelConfigFactory::detect())
    ->setUnsupportedPhpVersionAllowed(true);
