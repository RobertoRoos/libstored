#ifndef LIBSTORED_SPM_H
#define LIBSTORED_SPM_H
/*
 * libstored, a Store for Embedded Debugger.
 * Copyright (C) 2020-2021  Jochem Rutgers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifdef __cplusplus

#include <libstored/macros.h>
#include <libstored/util.h>

#include <list>
#include <new>

#ifdef STORED_HAVE_VALGRIND
#  include <valgrind/memcheck.h>
#endif

namespace stored {

	/*!
	 * \brief Memory that uses bump-alloc for a very fast short-lived heap.
	 *
	 * The ScratchPad grows automatically, but it is more efficient to manage
	 * the #capacity() on beforehand.  The capacity is determined while using
	 * the ScratchPad, which may cause some more overhead at the start of the
	 * application.
	 *
	 * There is no overhead per #alloc(), but padding bytes may be inserted to
	 * word-align allocs.  Heap fragmentation is not possible.
	 *
	 * Alloc is very fast, but dealloc or free is not possible.  Bump-alloc is
	 * like a stack; you can #reset() it, or make a #snapshot(), which you can
	 * rollback to.
	 *
	 * \tparam MaxSize the maximum total size to be allocated, which is used to
	 *         determine the type of the internal counters.
	 */
	template <size_t MaxSize=0xffff>
	class ScratchPad {
		CLASS_NOCOPY(ScratchPad)
	public:
		enum {
			/*! \brief Maximum total size of allocated memory. */
			maxSize = MaxSize,
			/*! \brief Size of the header of a chunk. */
			chunkHeader = sizeof(size_t),
			/*! \brief Extra amount to reserve when the chunk is allocated. */
			spare = 8 * sizeof(void*)
		};
		/*! \brief Type of all internally used size counters. */
		typedef typename value_type<MaxSize>::type size_type;

		/*!
		 * \brief Ctor.
		 * \param reserve number of bytes to reserve during construction
		 */
		explicit ScratchPad(size_t reserve = 0)
			: m_buffer()
			, m_size()
			, m_total()
			, m_max()
		{
			this->reserve(reserve);
		}

		/*!
		 * \brief Dtor.
		 */
		~ScratchPad() {
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
			free(chunk());

			for(std::list<char*>::iterator it = m_old.begin(); it != m_old.end(); ++it)
				free(chunk(*it)); // NOLINT(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
		}

		/*!
		 * \brief Resets the content of the ScratchPad.
		 *
		 * Coalesce chunks when required. It leaves #max() untouched.
		 * To actually free all used memory, call #shrink_to_fit() afterwards.
		 */
		void reset() {
			m_size = 0;
			m_total = 0;

			if(unlikely(!m_old.empty())) {
				// Coalesce chunks.
				for(std::list<char*>::iterator it = m_old.begin(); it != m_old.end(); ++it)
					free(chunk(*it)); // NOLINT(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)

				m_old.clear();
				reserve(m_max);
			}

#ifdef STORED_HAVE_VALGRIND
			if(m_buffer)
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast,hicpp-no-assembler)
				(void)VALGRIND_MAKE_MEM_NOACCESS(m_buffer, bufferSize());
#endif
		}

		/*!
		 * \brief Checks if the ScratchPad is empty.
		 */
		bool empty() const {
			return m_total == 0;
		}

		/*!
		 * \brief Returns the total amount of allocated memory.
		 * \details This includes padding because of alignment requirements of #alloc().
		 */
		size_t size() const {
			return (size_t)m_total;
		}

		/*!
		 * \brief Returns the maximum size.
		 * \details To reset this value, use #shrink_to_fit().
		 */
		size_t max() const {
			return (size_t)m_max;
		}

		/*!
		 * \brief Returns the total capacity currently available within the ScratchPad.
		 */
		size_t capacity() const {
			return (size_t)m_total - (size_t)m_size + (size_t)bufferSize();
		}

		/*!
		 * \brief A snapshot of the ScratchPad, which can be rolled back to.
		 *
		 * A Snapshot remains valid, until the ScratchPad is reset, or an
		 * earlier snapshot is rolled back.  An invalid snapshot cannot be
		 * rolled back, and cannot be destructed, as that implies a rollback.
		 * Make sure to reset the snapshot before destruction, if it may have
		 * become invalid.
		 *
		 * Normally, you would let a snapshot go out of scope before doing
		 * anything with older snapshots.
		 *
		 * \see #stored::ScratchPad::snapshot()
		 */
		class Snapshot {
		protected:
			friend class ScratchPad;
			/*!
			 * \brief Ctor.
			 */
			Snapshot(ScratchPad& spm, void* buffer, ScratchPad::size_type size) : m_spm(&spm), m_buffer(buffer), m_size(size) {}
		public:
			/*!
			 * \brief Dtor, which implies a rollback.
			 */
			~Snapshot() { rollback(); }

			/*!
			 * \brief Detach from the ScratchPad.
			 * \details Cannot rollback afterwards.
			 */
			void reset() { m_spm = nullptr; }

			/*!
			 * \brief Perform a rollback of the corresponding ScratchPad.
			 */
			void rollback() {
				if(m_spm)
					m_spm->rollback(m_buffer, m_size);
			}

#if STORED_cplusplus >= 201103L
			/*!
			 * \brief Move ctor.
			 */
			Snapshot(Snapshot&& s) noexcept : m_spm(s.m_spm), m_buffer(s.m_buffer), m_size(s.m_size) { s.reset(); }

			/*!
			 * \brief Move-assign.
			 */
			Snapshot& operator=(Snapshot&& s) noexcept {
				reset();
				m_spm = s.m_spm;
				m_buffer = s.m_buffer;
				m_size = s.m_size;
				s.reset();
				return *this;
			}
#endif
			/*!
			 * \brief Move ctor.
			 * \details Even though \p s is \c const, it will be reset anyway by this ctor.
			 */
			Snapshot(Snapshot const& s) : m_spm(s.m_spm), m_buffer(s.m_buffer), m_size(s.m_size) { s.reset(); }

		private:
			/*!
			 * \brief Resets and detaches this snapshot from the ScratchPad.
			 */
			void reset() const { m_spm = nullptr; }

#if STORED_cplusplus >= 201103L
		public:
			Snapshot& operator=(Snapshot const& s) = delete;
#else
		private:
			/*!
			 * \brief Move-assign.
			 * \details Even though \p s is \c const, it will be reset anyway by this operator.
			 */
			Snapshot& operator=(Snapshot const& s);
#endif

		private:
			/*! \brief The ScratchPad this is a snapshot of. */
			mutable ScratchPad* m_spm;
			/*! \brief The snapshot. */
			void* m_buffer;
			/*! \brief The total size of the ScratchPad when taking the snapshot. */
			ScratchPad::size_type m_size;
		};
		friend class Snapshot;

		/*!
		 * \brief Get a snapshot of the ScratchPad.
		 * \see #stored::ScratchPad::Snapshot.
		 */
		Snapshot snapshot() {
			return Snapshot(*this, empty() ? nullptr : &m_buffer[m_size], m_total);
		}

private:
		/*!
		 * \brief Perform a rollback to the given point.
		 * \see #stored::ScratchPad::Snapshot.
		 */
		void rollback(void* snapshot, size_type size) {
			if(!snapshot || !size) {
				reset();
				return;
			}

			char* snapshot_ = static_cast<char*>(snapshot);

			// Find correct buffer.
			while(!(snapshot_ >= m_buffer && snapshot_ < &m_buffer[bufferSize()]))
			{
				m_size = 0; // Don't care, will be recovered later on.
				bufferPop();
				stored_assert(m_buffer);
			}

			// Recover pointers within this buffer.
			stored_assert(size <= m_total);
			m_total = size;
			size_t size_ = (size_t)(snapshot_ - m_buffer);
			stored_assert(size_ < bufferSize());
			m_size = (size_type)size_;

#ifdef STORED_HAVE_VALGRIND
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast,hicpp-no-assembler)
			(void)VALGRIND_MAKE_MEM_NOACCESS(&m_buffer[m_size], bufferSize() - m_size);
#endif
		}

		/*!
		 * \brief Allocate a new buffer with the given size.
		 * \details The current buffer is moved to the #m_old list.
		 */
		void bufferPush(size_t size) {
			stored_assert(size > 0);

			if(m_buffer)
				m_old.push_back(m_buffer);

			// NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
			void* p = malloc(size + chunkHeader);
			if(!p) {
#ifdef __cpp_exceptions
				throw std::bad_alloc();
#else
				abort();
#endif
			}

			m_buffer = buffer(p);
			setBufferSize(size);
			m_size = 0;

#ifdef STORED_HAVE_VALGRIND
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast,hicpp-no-assembler)
			(void)VALGRIND_MAKE_MEM_NOACCESS(m_buffer, size);
#endif
		}

		/*!
		 * \brief Discard the current buffer and get the next one from the #m_old list.
		 */
		void bufferPop() {
			stored_assert(m_buffer || m_old.empty());

			if(m_buffer) {
				free(chunk(m_buffer)); // NOLINT(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
				m_buffer = nullptr;
			}

			if(!m_old.empty()) {
				m_buffer = m_old.back();
				m_old.pop_back();
			}

			stored_assert(m_size <= m_total);
			m_total = (size_type)(m_total - m_size);

			if(Config::Debug) {
				// We don't know m_size. Assume that this is part of a rollback(), which will set it properly.
				// Set it at worst-case now, such that the assert above may trigger.
				size_t b = bufferSize();
				stored_assert(b <= maxSize);
				m_size = (size_type)b;
			}
		}

		/*!
		 * \brief Try to grow (\c realloc()) the current buffer.
		 * \details The current buffer may be moved, which invalidates all allocations within the buffer.
		 */
		void bufferGrow(size_t size) {
			stored_assert(size > bufferSize());
			// clang-analyzer-unix.API: clang-tidy thinks new_cap can be 0, but that's not true.
			// NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,clang-analyzer-unix.API)
			void* p = realloc(chunk(m_buffer), size + chunkHeader);
			if(!p) {
#ifdef __cpp_exceptions
				throw std::bad_alloc();
#else
				abort();
#endif
			}

			m_buffer = buffer(p);
			setBufferSize(size);

#ifdef STORED_HAVE_VALGRIND
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast,hicpp-no-assembler)
			(void)VALGRIND_MAKE_MEM_NOACCESS(&m_buffer[m_size], size - m_size);
#endif
		}

		/*!
		 * \brief Returns the size of the current buffer.
		 */
		size_t bufferSize() const {
			return likely(m_buffer) ? *(size_t*)(chunk(m_buffer)) : 0;
		}

		/*!
		 * \brief Saves the malloc()ed size of the curretn buffer.
		 */
		void setBufferSize(size_t size) {
			stored_assert(m_buffer);
			stored_assert(size > 0);
			*(size_t*)(chunk(m_buffer)) = size;
		}

		/*!
		 * \brief Returns the chunk from the given buffer.
		 * \details The chunk is the actual piece of memory on the heap, which is the buffer with a header.
		 */
		static void* chunk(char* buffer) {
			return buffer ? buffer - chunkHeader : nullptr;
		}

		/*!
		 * \brief Returns the chunk from the current buffer.
		 * \details The chunk is the actual piece of memory on the heap, which is the buffer with a header.
		 */
		void* chunk() {
			return m_buffer ? chunk(m_buffer) : nullptr;
		}

		/*!
		 * \brief Returns the buffer within the given chunk.
		 */
		static char* buffer(void* chunk) {
			return static_cast<char*>(chunk) + chunkHeader;
		}

		/*!
		 * \brief Returns the current buffer.
		 */
		char* buffer() {
			return m_buffer;
		}

public:
		/*!
		 * \brief Reserves memory to save the additional given amount of bytes.
		 */
		void reserve(size_t more) {
			size_t new_cap = m_size + more;

			if(likely(new_cap <= bufferSize()))
				return;

			if(m_buffer && m_size == 0) {
				// Grow (realloc) is fine, as nobody is using the current buffer (if any).
				bufferGrow(new_cap);
			} else {
				// Grow (realloc) may move the buffer. So don't do that.
				new_cap = more + spare; // plus some extra reserve space
				bufferPush(new_cap);
			}
		}

		/*!
		 * \brief Releases all unused memory back to the OS, if possible.
		 */
		void shrink_to_fit() {
			if(unlikely(empty())) {
				m_max = 0;
				reset();
				// Also pop the current buffer.
				bufferPop();
				m_size = 0;
			} else {
				// realloc() may still return another chunk of memory, even if it gets smaller.
				// So, we cannot actually shrink, until reset() is called.
				m_max = m_total;
			}
		}

		/*!
		 * \brief Returns the number of chunks of the ScratchPad.
		 *
		 * You would want to have only one chunk, but during the first moments
		 * of running, the ScratchPad has to determine how much memory the
		 * application uses.  During this time, there may exist multiple
		 * chunks.  Call #reset() to optimize memory usage.
		 */
		size_t chunks() const {
			return m_old.size() + (m_buffer ? 1 : 0);
		}

		/*!
		 * \brief Allocate memory.
		 * \tparam T the type of object to allocate
		 * \param count number of objects, which is allocated as an array of \p T
		 * \param align alignment requirement (maximized to word size)
		 * \return a pointer to the allocated memory, which remains uninitialized and cannot be \c nullptr
		 */
		template <typename T>
		__attribute__((malloc,returns_nonnull,warn_unused_result))
		T* alloc(size_t count = 1, size_t align = sizeof(T)) {
			size_t alloc_size = count * sizeof(T);
			if(unlikely(alloc_size == 0)) {
				if(unlikely(!m_buffer))
					reserve(spare);
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				return reinterpret_cast<T*>(m_buffer);
			}

			align = std::max<size_t>(1, std::min(sizeof(void*), align));
			size_t padding = align - m_size % align;
			if(padding == align)
				padding = 0;

			if(unlikely(m_total + alloc_size + padding < m_total)) {
				// Wrap around -> overflow.
#ifdef __cpp_exceptions
				throw std::bad_alloc();
#else
				abort();
#endif
			}

			size_t bs = bufferSize();
			if(likely(m_size + padding <= bs)) {
				// The padding (which may be 0) still fits in the buffer.
				m_size = (size_type)(m_size + padding);
				// Now reserve the size, which still may add a new chunk.
				if(unlikely(m_size + alloc_size > bs))
					// Reserve all we probably need, if we are reserving anyway.
					reserve(std::max(max() - this->size(), alloc_size));
			} else {
				// Not enough room for the padding, let alone the size.
				// Just create a new buffer, which has always the correct alignment.
				bufferPush(std::max(max() - this->size(), alloc_size + spare));
			}

			char* p = m_buffer + m_size;
			m_size = (size_type)(m_size + alloc_size);

			// Do count the padding, even it was not allocated, as it might be
			// required anyway if the buffers are coalesced.
			m_total = (size_type)(m_total + padding + alloc_size);

			if(unlikely(m_total > m_max))
				m_max = m_total;

#ifdef STORED_HAVE_VALGRIND
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast,hicpp-no-assembler)
			(void)VALGRIND_MAKE_MEM_UNDEFINED(p, alloc_size);
#else
			if(Config::Debug)
				memset(p, 0xef, alloc_size);
#endif
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
			return reinterpret_cast<T*>(p);
		}

	private:
		/*! \brief Current buffer chunk. If it gets full, it is pushed onto #m_old and a new one is allocated. */
		char* m_buffer;
		/*! \brief Previous buffer chunks. */
		std::list<char*> m_old;
		/*! \brief Used offset within #m_buffer. */
		size_type m_size;
		/*! \brief Total memory usage of all chunks. */
		size_type m_total;
		/*! \brief Maximum value of #m_total. */
		size_type m_max;
	};

} // namespace
#endif // __cplusplus
#endif // LIBSTORED_SPM_H
