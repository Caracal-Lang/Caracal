#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <CaraReport.h>

#include <sstream>

namespace Caracal
{
    static CaraReport::Level ToCaraReportLevel(DiagnosticLevel level)
    {
        switch (level)
        {
            case DiagnosticLevel::Warning:
                return CaraReport::Level::Warning;
            case DiagnosticLevel::Error:
                return CaraReport::Level::Error;
            case DiagnosticLevel::Unknown:
            default:
                return CaraReport::Level::Info;
        }
    }

    static const SourceText& ResolveSource(
        const SourceTextSharedPtr& source,
        const SourceTextSharedPtr& diagnosticSource)
    {
        if (source)
        {
            return *source;
        }

        return *diagnosticSource;
    }

    static std::unique_ptr<CaraReport::Source> CreateSource(const SourceText& source)
    {
        if (source.filePath.empty())
        {
            return std::make_unique<CaraReport::Source>("<input>", source.text);
        }

        return std::make_unique<CaraReport::Source>(source.filePath.generic_string(), source.text);
    }

    // todo handle this in the carareport lib
    static SourceLocation FixUpLocation(const SourceLocation& location, const SourceText& source)
    {
        if (location.startIndex < 0 || location.endIndex < location.startIndex)
        {
            return {};
        }

        const auto sourceSize = static_cast<i32>(source.text.size());

        if (location.startIndex == location.endIndex && location.startIndex > 0)
        {
            if (location.startIndex <= sourceSize)
            {
                return {
                    .startIndex = location.startIndex - 1,
                    .endIndex = location.startIndex
                };
            }
        }

        if (location.endIndex > sourceSize)
        {
            return {
                .startIndex = location.startIndex > sourceSize ? sourceSize : location.startIndex,
                .endIndex = sourceSize
            };
        }

        return location;
    }

    static CaraReport::Span CreateSpan(const SourceLocation& location)
    {
        if (location.startIndex < 0 || location.endIndex < location.startIndex)
        {
            return CaraReport::Span{};
        }

        return CaraReport::Span::fromRange(location.startIndex, location.endIndex);
    }

    static CaraReport::Label CreateLabel(
        const Diagnostic::Label& label,
        const SourceTextSharedPtr& diagnosticSource)
    {
        const auto& sourceText = ResolveSource(label.source, diagnosticSource);
        const auto renderLocation = FixUpLocation(label.location, sourceText);
        return CaraReport::Label(
            CreateSpan(renderLocation),
            label.text,
            label.isPrimary);
    }

    static CaraReport::Report CreateReport(
        const Diagnostic& diagnostic,
        const SourceTextSharedPtr& diagnosticSource)
    {
        auto report = CaraReport::Report(diagnostic.message())
            .withTitle(std::string(diagnostic.code()))
            .withLevel(ToCaraReportLevel(diagnostic.level()));

        // without a source CaraReport prints no source lines, only the title, message and fix
        if (diagnosticSource)
        {
            static_cast<void>(report.withSource(CreateSource(*diagnosticSource)));
        }

        return report;
    }

    struct DiagnosticReport
    {
        CaraReport::Report report;
        std::vector<std::unique_ptr<CaraReport::Report>> relatedReports;
    };

    static std::unique_ptr<CaraReport::Report> CreateRelatedReport(
        const Diagnostic::RelatedReport& related,
        const SourceTextSharedPtr& diagnosticSource)
    {
        const auto& sourceText = ResolveSource(related.source, diagnosticSource);
        auto report = std::make_unique<CaraReport::Report>(related.message);
        static_cast<void>(report->withTitle("Related"));
        static_cast<void>(report->withLevel(CaraReport::Level::Info));
        static_cast<void>(report->withSource(CreateSource(sourceText)));

        for (const auto& label : related.labels)
        {
            static_cast<void>(report->withLabel(CreateLabel(label, diagnosticSource)));
        }

        return report;
    }

    static DiagnosticReport BuildReport(const Diagnostic& diagnostic)
    {
        const auto& diagnosticSource = diagnostic.source();
        auto diagnosticReport = DiagnosticReport{
            .report = CreateReport(diagnostic, diagnosticSource),
            .relatedReports = {}
        };

        for (const auto& label : diagnostic.labels())
        {
            static_cast<void>(diagnosticReport.report.withLabel(CreateLabel(label, diagnosticSource)));
        }

        for (const auto& related : diagnostic.related())
        {
            auto relatedReport = CreateRelatedReport(related, diagnosticSource);
            static_cast<void>(diagnosticReport.report.withRelated(relatedReport.get()));
            diagnosticReport.relatedReports.push_back(std::move(relatedReport));
        }

        if (diagnostic.fix().has_value())
        {
            static_cast<void>(diagnosticReport.report.withFix(diagnostic.fix().value()));
        }

        return diagnosticReport;
    }

    void writeDiagnostic(
        std::ostream& outStream,
        const Diagnostic& diagnostic,
        const DiagnosticOptions& options)
    {
        auto diagnosticReport = BuildReport(diagnostic);
        auto writer = CaraReport::Writer::create()
            .withContextLines(options.contextLines)
            .withColors(options.enableColors)
            .withUnicode(options.enableUnicode);

        outStream << writer.writeReport(diagnosticReport.report);
    }

    void writeDiagnostics(
        std::ostream& outStream,
        DiagnosticsBag& diagnostics,
        const DiagnosticOptions& options)
    {
        auto writer = CaraReport::Writer::create()
            .withContextLines(options.contextLines)
            .withColors(options.enableColors)
            .withUnicode(options.enableUnicode);

        const auto& entries = diagnostics.diagnostics();
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            auto diagnosticReport = BuildReport(entries[i]);
            outStream << writer.writeReport(diagnosticReport.report);
        }
    }

    std::string formatDiagnostic(
        const Diagnostic& diagnostic,
        const DiagnosticOptions& options)
    {
        std::ostringstream outStream;
        writeDiagnostic(outStream, diagnostic, options);
        return outStream.str();
    }

    std::string formatDiagnostics(
        DiagnosticsBag& diagnostics,
        const DiagnosticOptions& options)
    {
        std::ostringstream outStream;
        writeDiagnostics(outStream, diagnostics, options);
        return outStream.str();
    }
}
