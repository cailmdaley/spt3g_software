#include <pybindings.h>
#include <G3Writer.h>

#include <boost/algorithm/string.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#ifdef BZIP2_FOUND
#include <boost/iostreams/filter/bzip2.hpp>
#endif
#include <boost/filesystem.hpp>

G3Writer::G3Writer(std::string filename,
    std::vector<G3Frame::FrameType> streams,
    bool append) :
    filename_(filename), streams_(streams)
{
	boost::filesystem::path fpath(filename);
	if (fpath.empty() || (fpath.has_parent_path() &&
	    !boost::filesystem::exists(fpath.parent_path())))
		throw std::runtime_error(std::string("Parent path does not "
		    "exist: ") + fpath.parent_path().string());

	if (boost::algorithm::ends_with(filename, ".gz") && !append)
		stream_.push(boost::iostreams::gzip_compressor());
	if (boost::algorithm::ends_with(filename, ".bz2") && !append) {
#ifdef BZIP2_FOUND
		stream_.push(boost::iostreams::bzip2_compressor());
#else
		log_fatal("Boost not compiled with bzip2 support.");
#endif
	}

	std::ios_base::openmode mode = std::ios::binary;
	if (append)
		mode |= std::ios::app;
	stream_.push(boost::iostreams::file_sink(filename, mode));
}

G3Writer::~G3Writer()
{
	stream_.reset();
}

void G3Writer::Process(G3FramePtr frame, std::deque<G3FramePtr> &out)
{
	// If in Python context, release the GIL while writing out.
	// Note we must force serialization of the frames, first,
	// because in some cases serialization requires calling back
	// out to Python.
	frame->GenerateBlobs();
	PyThreadState *_save = nullptr;
	if (Py_IsInitialized() && PyGILState_Check())
		_save = PyEval_SaveThread();

	if (frame->type == G3Frame::EndProcessing)
		stream_.reset();
	else if (streams_.size() == 0 ||
	    std::find(streams_.begin(), streams_.end(), frame->type) !=
	    streams_.end())
		frame->save(stream_);

	out.push_back(frame);

	if (_save != nullptr)
		PyEval_RestoreThread(_save);
}

void G3Writer::Flush()
{
    if (!stream_.strict_sync()){
        printf("There was a problem flushing the stream...\n");
    }
}

PYBINDINGS("core") {
	using namespace boost::python;

	// Instead of EXPORT_G3MODULE since there is an extra Flush function
	class_<G3Writer, bases<G3Module>, boost::shared_ptr<G3Writer>,
	    boost::noncopyable>("G3Writer",
	      "Writes frames to disk. Frames will be written to the file specified by "
	      "filename. If filename ends in .gz, output will be compressed using gzip. "
	      "To write only some types of frames, pass a list of the desired frame "
	      "types to the second optional argument (streams). If no streams argument "
	      "is given, writes all types of frames. If append is set to True, will "
	      "append frames to its output file rather than overwriting it.",
        init<std::string, std::vector<G3Frame::FrameType>, bool>((arg("filename"),
	    arg("streams")=std::vector<G3Frame::FrameType>(), arg("append")=false)))
        .def("Flush", &G3Writer::Flush)
        .def_readonly("__g3module__", true)
	;
}
