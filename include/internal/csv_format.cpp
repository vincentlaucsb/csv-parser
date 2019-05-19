#include "csv_format.hpp"

namespace csv {
    CSVFormat create_default_csv_strict() {
        CSVFormat format;
        format.delimiter(',')
            .quote('"')
            .header_row(0)
            .detect_bom(true)
            .strict_parsing(true);

        return format;
    }

    CSVFormat create_guess_csv() {
        CSVFormat format;
        format.delimiter({ ',', '|', '\t', ';', '^' })
            .quote('"')
            .header_row(0)
            .detect_bom(true);

        return format;
    }

    const CSVFormat CSVFormat::RFC4180_STRICT = create_default_csv_strict();
    const CSVFormat CSVFormat::GUESS_CSV = create_guess_csv();
}