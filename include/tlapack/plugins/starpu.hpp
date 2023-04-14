/// @file mdspan.hpp
/// @author Weslley S Pereira, University of Colorado Denver, USA
//
// Copyright (c) 2021-2023, University of Colorado Denver. All rights reserved.
//
// This file is part of <T>LAPACK.
// <T>LAPACK is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.

#ifndef TLAPACK_STARPU_HH
#define TLAPACK_STARPU_HH

#include <starpu.h>

#include <iomanip>
#include <ostream>
#include <tuple>
#include <type_traits>

#include "tlapack/base/arrayTraits.hpp"
#include "tlapack/base/workspace.hpp"

namespace tlapack {

namespace starpu {

    namespace internal {

        enum class Operation { Assign, Add, Subtract, Multiply, Divide };

        /// @brief Convert an Operation to a string
        std::string to_string(Operation v)
        {
            switch (v) {
                case Operation::Assign:
                    return "assign";
                case Operation::Add:
                    return "add";
                case Operation::Subtract:
                    return "subtract";
                case Operation::Multiply:
                    return "multiply";
                case Operation::Divide:
                    return "divide";
            }
            return "unknown";
        }

        /// @brief Convert an Operation to an output stream
        inline std::ostream& operator<<(std::ostream& out, Operation v)
        {
            return out << to_string(v);
        }

        /// Return an empty starpu_codelet struct
        constexpr struct starpu_codelet codelet_init() noexcept
        {
            return {
                0,                      // where
                NULL,                   // can_execute
                starpu_codelet_type(),  // type
                0,                      // max_parallelism
                NULL,                   // cpu_func STARPU_DEPRECATED
                NULL,                   // cuda_func STARPU_DEPRECATED
                NULL,                   // opencl_func STARPU_DEPRECATED
                {},                     // cpu_funcs
                {},                     // cuda_funcs
                {},                     // cuda_flags
                {},                     // hip_funcs
                {},                     // hip_flags
                {},                     // opencl_funcs
                {},                     // opencl_flags
                {},                     // max_fpga_funcs
                {},                     // cpu_funcs_name
                NULL,                   // bubble_func
                NULL,                   // bubble_gen_dag_func
                0,                      // nbuffers
                {},                     // modes
                NULL,                   // dyn_modes
                0,                      // specific_nodes
                {},                     // nodes
                NULL,                   // dyn_nodes
                NULL,                   // model
                NULL,                   // energy_model
                {},                     // per_worker_stats
                "",                     // name
                0,                      // color
                NULL,                   // callback_func
                0,                      // flags
                NULL,                   // perf_counter_sample
                NULL,                   // perf_counter_values
                0                       // checked
            };
        }

        /// Struct to store codelet and value
        template <class T>
        struct cl_value {
            struct starpu_codelet cl;
            T value;
        };

    }  // namespace internal

    namespace cpu {

        /**
         * @brief Data operation with assignment using two StarPU variable
         * buffers
         *
         * This function is used to perform data operations on Matrix<T>::data.
         * The interface is suitable for tasks that are submitted to StarPU.
         */
        template <class T, internal::Operation op>
        constexpr void data_op_data(void** buffers, void* args) noexcept
        {
            using internal::Operation;
            T* x = (T*)STARPU_VARIABLE_GET_PTR(buffers[0]);
            if constexpr (op == internal::Operation::Assign)
                *x = *((T*)STARPU_VARIABLE_GET_PTR(buffers[1]));
            else if constexpr (op == internal::Operation::Add)
                *x += *((T*)STARPU_VARIABLE_GET_PTR(buffers[1]));
            else if constexpr (op == internal::Operation::Subtract)
                *x -= *((T*)STARPU_VARIABLE_GET_PTR(buffers[1]));
            else if constexpr (op == internal::Operation::Multiply)
                *x *= *((T*)STARPU_VARIABLE_GET_PTR(buffers[1]));
            else if constexpr (op == internal::Operation::Divide)
                *x /= *((T*)STARPU_VARIABLE_GET_PTR(buffers[1]));
        }

        /**
         * @brief Data operation with assignment using a StarPU variable buffer
         * and a value
         *
         * This function is used to perform data operations on Matrix<T>::data.
         * The interface is suitable for tasks that are submitted to StarPU.
         */
        template <class T, internal::Operation op>
        constexpr void data_op_value(void** buffers, void* args) noexcept
        {
            using internal::Operation;
            T* x = (T*)STARPU_VARIABLE_GET_PTR(buffers[0]);
            if constexpr (op == internal::Operation::Assign)
                *x = *((T*)args);
            else if constexpr (op == internal::Operation::Add)
                *x += *((T*)args);
            else if constexpr (op == internal::Operation::Subtract)
                *x -= *((T*)args);
            else if constexpr (op == internal::Operation::Multiply)
                *x *= *((T*)args);
            else if constexpr (op == internal::Operation::Divide)
                *x /= *((T*)args);
        }

        constexpr void free_cl(void* args) noexcept
        {
            free((struct starpu_codelet*)args);
        }

        template <class T>
        constexpr void free_cl_value(void* args) noexcept
        {
            free((internal::cl_value<T>*)args);
        }

    }  // namespace cpu

    namespace internal {

        template <class T, bool TisConstType>
        struct EntryAccess;

        template <class T>
        struct EntryAccess<T, true> {
            using idx_t = uint32_t;
            struct data;

            // abstract interface
            virtual idx_t nrows() const noexcept = 0;
            virtual idx_t ncols() const noexcept = 0;
            virtual idx_t nblockrows() const noexcept = 0;
            virtual idx_t nblockcols() const noexcept = 0;

            // operator() and operator[]

            /**
             * @brief Returns an element of the matrix
             *
             * @param[in] i Row index
             * @param[in] j Column index
             *
             * @return The value of the element at position (i,j)
             */
            constexpr T operator()(idx_t i, idx_t j) const noexcept
            {
                assert((i >= 0 && i < nrows()) && "Row index out of bounds");
                assert((j >= 0 && j < ncols()) && "Column index out of bounds");

                const idx_t mb = nblockrows();
                const idx_t nb = nblockcols();
                const idx_t pos[2] = {i % mb, j % nb};

                const starpu_data_handle_t root_handle =
                    get_tile_handle(i / mb, j / nb);

                return T(data(root_handle, pos));
            }

            /**
             * @brief Returns an element of the vector
             *
             * @param[in] i index
             *
             * @return The value of the element at position i
             */
            constexpr T operator[](idx_t i) const noexcept
            {
                assert((nrows() <= 1 || ncols() <= 1) &&
                       "Matrix is not a vector");
                return (nrows() > 1) ? (*this)(i, 0) : (*this)(0, i);
            }

           private:
            virtual starpu_data_handle_t get_tile_handle(
                idx_t i, idx_t j) const noexcept = 0;
        };

        template <class T>
        struct EntryAccess<T, false> : public EntryAccess<T, true> {
            using idx_t = EntryAccess<T, true>::idx_t;
            using data = EntryAccess<T, true>::data;
            using EntryAccess<T, true>::operator();
            using EntryAccess<T, true>::operator[];

            // abstract interface
            virtual idx_t nrows() const noexcept = 0;
            virtual idx_t ncols() const noexcept = 0;
            virtual idx_t nblockrows() const noexcept = 0;
            virtual idx_t nblockcols() const noexcept = 0;

            // operator() and operator[]

            /**
             * @brief Returns a reference to an element of the matrix
             *
             * @param[in] i Row index
             * @param[in] j Column index
             *
             * @return A reference to the element at position (i,j)
             */
            constexpr data operator()(idx_t i, idx_t j) noexcept
            {
                assert((i >= 0 && i < nrows()) && "Row index out of bounds");
                assert((j >= 0 && j < ncols()) && "Column index out of bounds");

                const idx_t mb = nblockrows();
                const idx_t nb = nblockcols();
                const idx_t pos[2] = {i % mb, j % nb};

                const starpu_data_handle_t root_handle =
                    get_tile_handle(i / mb, j / nb);

                return data(root_handle, pos);
            }

            /**
             * @brief Returns a reference to an element of the vector
             *
             * @param[in] i index
             *
             * @return A reference to the element at position i
             */
            constexpr data operator[](idx_t i) noexcept
            {
                assert((nrows() <= 1 || ncols() <= 1) &&
                       "Matrix is not a vector");
                return (nrows() > 1) ? (*this)(i, 0) : (*this)(0, i);
            }

           private:
            virtual starpu_data_handle_t get_tile_handle(
                idx_t i, idx_t j) const noexcept = 0;
        };

        /**
         * @brief Arithmetic data type used by Matrix
         *
         * This is a wrapper around StarPU variable handles. It is used to
         * perform arithmetic operations on data types stored in StarPU
         * matrices. It uses StarPU tasks to perform the operations.
         *
         * @note Mind that operations between variables may create a large
         * overhead due to the creation of StarPU tasks.
         */
        template <typename T>
        struct EntryAccess<T, true>::data {
            const starpu_data_handle_t root_handle;  ///< Matrix handle
            starpu_data_handle_t handle;             ///< Variable handle

            /// @brief Data constructor from a variable handle
            constexpr explicit data(starpu_data_handle_t root_handle,
                                    const idx_t pos[2]) noexcept
                : root_handle(root_handle)
            {
                struct starpu_data_filter f_var = var_filter((void*)pos);
                starpu_data_partition_plan(root_handle, &f_var, &handle);
            }

            // Disable copy and move constructors
            data(data&&) = delete;
            data(const data&) = delete;

            /// Destructor cleans StarPU partition plan
            ~data() noexcept
            {
                starpu_data_partition_clean(root_handle, 1, &handle);
            }

            /// Implicit conversion to T
            constexpr operator T() const noexcept
            {
                starpu_data_acquire(handle, STARPU_R);
                const T x = *((T*)starpu_variable_get_local_ptr(handle));
                starpu_data_release(handle);

                return x;
            }

            // Arithmetic operators with assignment

            constexpr data& operator=(const data& x) noexcept
            {
                return operate_and_assign<Operation::Assign>(x);
            }
            constexpr data& operator=(const T& x) noexcept
            {
                return operate_and_assign<Operation::Assign>(x);
            }
            constexpr data& operator=(data&& x) noexcept
            {
                return (*this = (const data&)x);
            }

            constexpr data& operator+=(const data& x) noexcept
            {
                return operate_and_assign<Operation::Add>(x);
            }
            constexpr data& operator+=(const T& x) noexcept
            {
                return operate_and_assign<Operation::Add>(x);
            }

            constexpr data& operator-=(const data& x) noexcept
            {
                return operate_and_assign<Operation::Subtract>(x);
            }
            constexpr data& operator-=(const T& x) noexcept
            {
                return operate_and_assign<Operation::Subtract>(x);
            }

            constexpr data& operator*=(const data& x) noexcept
            {
                return operate_and_assign<Operation::Multiply>(x);
            }
            constexpr data& operator*=(const T& x) noexcept
            {
                return operate_and_assign<Operation::Multiply>(x);
            }

            constexpr data& operator/=(const data& x) noexcept
            {
                return operate_and_assign<Operation::Divide>(x);
            }
            constexpr data& operator/=(const T& x) noexcept
            {
                return operate_and_assign<Operation::Divide>(x);
            }

            // Other math functions

            constexpr friend T abs(const data& x) noexcept { return abs(T(x)); }
            constexpr friend T sqrt(const data& x) noexcept
            {
                return sqrt(T(x));
            }

            // Display value in ostream

            constexpr friend std::ostream& operator<<(std::ostream& out,
                                                      const data& x)
            {
                return out << T(x);
            }

           private:
            /// @brief Generates a StarPU codelet for a given operation with a
            /// value
            /// @tparam op Operation to perform
            template <Operation op>
            static constexpr struct starpu_codelet gen_cl_op_value() noexcept
            {
                struct starpu_codelet cl = codelet_init();

                cl.cpu_funcs[0] = cpu::data_op_value<T, op>;
                cl.nbuffers = 1;
                cl.modes[0] = (op == Operation::Assign) ? STARPU_W : STARPU_RW;
                cl.name = (op == Operation::Assign)
                              ? "assign_value"
                              : (op == Operation::Add)
                                    ? "add_value"
                                    : (op == Operation::Subtract)
                                          ? "subtract_value"
                                          : (op == Operation::Multiply)
                                                ? "multiply_value"
                                                : (op == Operation::Divide)
                                                      ? "divide_value"
                                                      : "unknown";

                return cl;
            }

            /// @brief Generates a StarPU codelet for a given operation with
            /// another variable
            /// @tparam op Operation to perform
            template <Operation op>
            static constexpr struct starpu_codelet gen_cl_op_data() noexcept
            {
                struct starpu_codelet cl = codelet_init();

                cl.cpu_funcs[0] = cpu::data_op_data<T, op>;
                cl.nbuffers = 2;
                cl.modes[0] = (op == Operation::Assign) ? STARPU_W : STARPU_RW;
                cl.modes[1] = STARPU_R;
                cl.name = (op == Operation::Assign)
                              ? "assign_data"
                              : (op == Operation::Add)
                                    ? "add_data"
                                    : (op == Operation::Subtract)
                                          ? "subtract_data"
                                          : (op == Operation::Multiply)
                                                ? "multiply_data"
                                                : (op == Operation::Divide)
                                                      ? "divide_data"
                                                      : "unknown";

                return cl;
            }

            /**
             * @brief Applies an operation and assigns
             *
             * Operations: +, -, *, /
             *
             * @tparam op  Operation
             * @param x   Second operand
             * @return constexpr data&  Reference to the result
             */
            template <Operation op>
            constexpr data& operate_and_assign(const data& x) noexcept
            {
                // Allocate space for the codelet
                struct starpu_codelet* cl = (struct starpu_codelet*)malloc(
                    sizeof(struct starpu_codelet));
                if (cl == nullptr) {
                    std::cerr << "Error allocating memory for codelet"
                              << std::endl;
                    starpu_shutdown();
                    exit(1);
                }

                // Initialize codelet
                *cl = gen_cl_op_data<op>();

                struct starpu_task* task = starpu_task_create();
                task->cl = cl;
                task->handles[0] = this->handle;
                task->handles[1] = x.handle;
                task->synchronous = false;
                task->callback_func = cpu::free_cl;
                task->callback_arg = (void*)cl;
                const int ret = starpu_task_submit(task);

                STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

                return *this;
            }

            /**
             * @brief Applies an operation and assigns
             *
             * Operations: +, -, *, /
             *
             * @tparam op  Operation
             * @param x   Second operand
             * @return constexpr data&  Reference to the result
             */
            template <Operation op>
            constexpr data& operate_and_assign(const T& x) noexcept
            {
                // Allocate space for the codelet and value
                cl_value<T>* callback_arg =
                    (cl_value<T>*)malloc(sizeof(cl_value<T>));
                if (callback_arg == nullptr) {
                    std::cerr << "Error allocating memory for callback_arg"
                              << std::endl;
                    starpu_shutdown();
                    exit(1);
                }

                // Initialize codelet and value
                callback_arg->cl = gen_cl_op_value<op>();
                callback_arg->value = x;

                struct starpu_task* task = starpu_task_create();
                task->cl = &(callback_arg->cl);
                task->handles[0] = this->handle;
                task->synchronous = false;
                task->cl_arg = (void*)&(callback_arg->value);
                task->cl_arg_size = sizeof(T);
                task->callback_func = cpu::free_cl_value<T>;
                task->callback_arg = (void*)callback_arg;
                const int ret = starpu_task_submit(task);

                STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");

                return *this;
            }

            constexpr struct starpu_data_filter var_filter(void* pos) noexcept
            {
                return {.filter_func = starpu_matrix_filter_pick_variable,
                        .nchildren = 1,
                        .get_child_ops =
                            starpu_matrix_filter_pick_variable_child_ops,
                        .filter_arg_ptr = pos};
            }
        };

    }  // namespace internal

    template <class T>
    class Matrix : public internal::EntryAccess<T, std::is_const_v<T>> {
       public:
        using idx_t = internal::EntryAccess<T, true>::idx_t;
        using internal::EntryAccess<T, std::is_const_v<T>>::operator();
        using internal::EntryAccess<T, std::is_const_v<T>>::operator[];

        // ---------------------------------------------------------------------
        // Constructors and destructor

        /// Create a matrix of size m-by-n from a pointer in main memory
        constexpr Matrix(T* ptr, idx_t m, idx_t n, idx_t ld) noexcept
            : is_owner(true)
        {
            starpu_matrix_data_register(&handle, STARPU_MAIN_RAM,
                                        (uintptr_t)ptr, ld, m, n, sizeof(T));
        }

        /// Create a matrix of size m-by-n from contiguous data in main memory
        constexpr Matrix(T* ptr, idx_t m, idx_t n) noexcept
            : Matrix(ptr, m, n, m)
        {}

        /// Create a submatrix from a handle and a grid
        constexpr Matrix(starpu_data_handle_t handle,
                         idx_t ix,
                         idx_t iy,
                         idx_t nx,
                         idx_t ny) noexcept
            : handle(handle), is_owner(false), ix(ix), iy(iy), nx(nx), ny(ny)
        {}

        // Disable copy assignment operator
        Matrix& operator=(const Matrix&) = delete;

        /// Matrix destructor unpartitions and unregisters the data handle
        ~Matrix() noexcept
        {
            if (is_owner) {
                if (is_partitioned())
                    starpu_data_unpartition(handle, STARPU_MAIN_RAM);
                starpu_data_unregister(handle);
            }
        }

        // ---------------------------------------------------------------------
        // Create grid and get tile

        /// Tells whether the matrix is partitioned
        constexpr bool is_partitioned() const noexcept
        {
            return starpu_data_get_nb_children(handle) > 0;
        }

        /**
         * @brief Create a grid in the StarPU handle
         *
         * This function creates a grid that partitions the matrix into nx*ny
         * tiles. If the matrix is m-by-n, then every tile (i,j) from 0 <= i <
         * m-1 and 0 <= j < n-1 is a matrix (m/nx)-by-(n/ny). The tiles where i
         * = nx-1 or j = ny-1 are special, as they may have a smaller sizes.
         *
         * @param[in] nx Partitions in x
         * @param[in] ny Partitions in y
         */
        void create_grid(idx_t nx, idx_t ny) noexcept
        {
            assert(!is_partitioned() &&
                   "Cannot partition a partitioned matrix");
            assert(nx > 0 && ny > 0 && "Number of tiles must be positive");

            /* Split into blocks of complete rows first */
            const struct starpu_data_filter row_split = {
                .filter_func = starpu_matrix_filter_block, .nchildren = nx};

            /* Then split rows into tiles */
            const struct starpu_data_filter col_split = {
                .filter_func = starpu_matrix_filter_vertical_block,
                .nchildren = ny};

            /// TODO: This is not the correct function to use. It only works if
            /// m is a multiple of nx and n is a multiple of ny. We may need to
            /// use another filter.
            starpu_data_map_filters(handle, 2, &row_split, &col_split);

            this->nx = nx;
            this->ny = ny;
        }

        /// Get number of tiles in x direction
        constexpr idx_t get_nx() const noexcept { return nx; }

        /// Get number of tiles in y direction
        constexpr idx_t get_ny() const noexcept { return ny; }

        /// Get the maximum number of rows of a tile
        constexpr idx_t nblockrows() const noexcept
        {
            return starpu_matrix_get_nx(
                (is_partitioned()) ? starpu_data_get_child(handle, 0) : handle);
        }

        /// Get the maximum number of columns of a tile
        constexpr idx_t nblockcols() const noexcept
        {
            if (is_partitioned()) {
                const starpu_data_handle_t x0 =
                    starpu_data_get_child(handle, 0);
                return starpu_matrix_get_ny(
                    (starpu_data_get_nb_children(x0) > 0)
                        ? starpu_data_get_child(x0, 0)
                        : x0);
            }
            else
                return starpu_matrix_get_ny(handle);
        }

        // ---------------------------------------------------------------------
        // Get number of rows and columns

        /**
         * @brief Get the number of rows in the matrix
         *
         * @return Number of rows in the matrix
         */
        idx_t nrows() const noexcept
        {
            const idx_t NX = starpu_data_get_nb_children(handle);
            if (NX <= 1) {
                return starpu_matrix_get_nx(handle);
            }
            else {
                const idx_t nb =
                    starpu_matrix_get_nx(starpu_data_get_child(handle, 0));
                if (ix + nx < NX)
                    return nx * nb;
                else
                    return (nx - 1) * nb +
                           starpu_matrix_get_nx(
                               starpu_data_get_child(handle, NX - 1));
            }
        }

        /**
         * @brief Get the number of columns in the matrix
         *
         * @return Number of columns in the matrix
         */
        idx_t ncols() const noexcept
        {
            const idx_t NX = starpu_data_get_nb_children(handle);
            if (NX <= 1) {
                return starpu_matrix_get_ny(handle);
            }
            else {
                starpu_data_handle_t x0 = starpu_data_get_child(handle, 0);
                const idx_t NY = starpu_data_get_nb_children(x0);
                if (NY <= 1) {
                    return starpu_matrix_get_ny(x0);
                }
                else {
                    const idx_t nb =
                        starpu_matrix_get_ny(starpu_data_get_child(x0, 0));
                    if (iy + ny < NY)
                        return ny * nb;
                    else
                        return (ny - 1) * nb +
                               starpu_matrix_get_ny(
                                   starpu_data_get_child(x0, NY - 1));
                }
            }
        }

        // ---------------------------------------------------------------------
        // Submatrix creation

        /**
         * @brief Create a submatrix when the matrix is partitioned into tiles
         *
         * @param[in] ix Index of the first tile in x
         * @param[in] iy Index of the first tile in y
         * @param[in] nx Number of tiles in x
         * @param[in] ny Number of tiles in y
         *
         */
        constexpr Matrix<T> get_tiles(idx_t ix,
                                      idx_t iy,
                                      idx_t nx,
                                      idx_t ny) noexcept
        {
            assert(is_partitioned() && "Matrix is not partitioned");
            assert(nx >= 0 && ny >= 0 &&
                   "Number of tiles must be positive or 0");
            assert((ix >= 0 && ix + nx <= this->nx) &&
                   "Submatrix out of bounds");
            assert((iy >= 0 && iy + ny <= this->ny) &&
                   "Submatrix out of bounds");

            return Matrix<T>(handle, this->ix + ix, this->iy + iy, nx, ny);
        }

        /**
         * @brief Create a const submatrix when the matrix is partitioned into
         * tiles
         *
         * @param[in] ix Index of the first tile in x
         * @param[in] iy Index of the first tile in y
         * @param[in] nx Number of tiles in x
         * @param[in] ny Number of tiles in y
         *
         */
        constexpr Matrix<const T> get_const_tiles(idx_t ix,
                                                  idx_t iy,
                                                  idx_t nx,
                                                  idx_t ny) const noexcept
        {
            assert(is_partitioned() && "Matrix is not partitioned");
            assert(nx >= 0 && ny >= 0 &&
                   "Number of tiles must be positive or 0");
            assert((ix >= 0 && ix + nx <= this->nx) &&
                   "Submatrix out of bounds");
            assert((iy >= 0 && iy + ny <= this->ny) &&
                   "Submatrix out of bounds");

            return Matrix<const T>(handle, this->ix + ix, this->iy + iy, nx,
                                   ny);
        }

        // ---------------------------------------------------------------------
        // Display matrix in output stream

        friend std::ostream& operator<<(std::ostream& out,
                                        const starpu::Matrix<T>& A)
        {
            using idx_t = typename starpu::Matrix<T>::idx_t;
            out << "starpu::Matrix<" << typeid(T).name()
                << ">( nrows = " << A.nrows() << ", ncols = " << A.ncols()
                << " )";
            if (A.ncols() <= 10) {
                out << std::scientific << std::setprecision(2) << "\n";
                for (idx_t i = 0; i < A.nrows(); ++i) {
                    for (idx_t j = 0; j < A.ncols(); ++j) {
                        const T number = A(i, j);
                        if (!std::signbit(number)) out << " ";
                        out << number << " ";
                    }
                    out << "\n";
                }
            }
            return out;
        }

       private:
        starpu_data_handle_t handle;  ///< Data handle
        const bool is_owner =
            false;  ///< Whether this object owns the data handle

        const idx_t ix = 0;  ///< Index of the first tile in the x direction
        const idx_t iy = 0;  ///< Index of the first tile in the y direction
        idx_t nx = 1;        ///< Number of tiles in the x direction
        idx_t ny = 1;        ///< Number of tiles in the y direction

        /// Get the data handle of a tile in the matrix or the data handle of
        /// the matrix if it is not partitioned
        constexpr starpu_data_handle_t get_tile_handle(idx_t i,
                                                       idx_t j) const noexcept
        {
            return (is_partitioned())
                       ? starpu_data_get_sub_data(handle, 2, ix + i, iy + j)
                       : handle;
        }
    };

}  // namespace starpu

// -----------------------------------------------------------------------------
// Data descriptors

// Number of rows
template <class T>
constexpr auto nrows(const starpu::Matrix<T>& A)
{
    return A.nrows();
}
// Number of columns
template <class T>
constexpr auto ncols(const starpu::Matrix<T>& A)
{
    return A.ncols();
}
// Size
template <class T>
constexpr auto size(const starpu::Matrix<T>& A)
{
    return A.nrows() * A.ncols();
}

// -----------------------------------------------------------------------------
// Block operations for starpu::Matrix

#define isSlice(SliceSpec) \
    std::is_convertible<SliceSpec, std::tuple<uint32_t, uint32_t>>::value

template <
    class T,
    class SliceSpecRow,
    class SliceSpecCol,
    typename std::enable_if<isSlice(SliceSpecRow) && isSlice(SliceSpecCol),
                            int>::type = 0>
constexpr auto slice(const starpu::Matrix<T>& A,
                     SliceSpecRow&& rows,
                     SliceSpecCol&& cols)
{
    const uint32_t row0 = std::get<0>(rows);
    const uint32_t col0 = std::get<0>(cols);
    const uint32_t row1 = std::get<1>(rows);
    const uint32_t col1 = std::get<1>(cols);
    const uint32_t nrows = row1 - row0;
    const uint32_t ncols = col1 - col0;

    const uint32_t mb = A.nblockrows();
    const uint32_t nb = A.nblockcols();

    tlapack_check(row0 % mb == 0);
    tlapack_check(col0 % nb == 0);
    tlapack_check((nrows % mb == 0) ||
                  (row1 == A.nrows() && ((nrows - 1) % mb == 0)));
    tlapack_check((ncols % nb == 0) ||
                  (col1 == A.ncols() && ((ncols - 1) % nb == 0)));

    return A.get_const_tiles(
        row0 / mb, col0 / nb,
        (nrows == 0) ? 0 : std::max<uint32_t>(nrows / mb, 1),
        (ncols == 0) ? 0 : std::max<uint32_t>(ncols / nb, 1));
}
template <
    class T,
    class SliceSpecRow,
    class SliceSpecCol,
    typename std::enable_if<isSlice(SliceSpecRow) && isSlice(SliceSpecCol),
                            int>::type = 0>
constexpr auto slice(starpu::Matrix<T>& A,
                     SliceSpecRow&& rows,
                     SliceSpecCol&& cols)
{
    const uint32_t row0 = std::get<0>(rows);
    const uint32_t col0 = std::get<0>(cols);
    const uint32_t row1 = std::get<1>(rows);
    const uint32_t col1 = std::get<1>(cols);
    const uint32_t nrows = row1 - row0;
    const uint32_t ncols = col1 - col0;

    const uint32_t mb = A.nblockrows();
    const uint32_t nb = A.nblockcols();

    tlapack_check(row0 % mb == 0);
    tlapack_check(col0 % nb == 0);
    tlapack_check((nrows % mb == 0) ||
                  (row1 == A.nrows() && ((nrows - 1) % mb == 0)));
    tlapack_check((ncols % nb == 0) ||
                  (col1 == A.ncols() && ((ncols - 1) % nb == 0)));

    return A.get_tiles(row0 / mb, col0 / nb,
                       (nrows == 0) ? 0 : std::max<uint32_t>(nrows / mb, 1),
                       (ncols == 0) ? 0 : std::max<uint32_t>(ncols / nb, 1));
}

#undef isSlice

template <class T, class SliceSpec>
constexpr auto slice(const starpu::Matrix<T>& v,
                     SliceSpec&& range,
                     uint32_t colIdx)
{
    return slice(v, std::forward<SliceSpec>(range),
                 std::make_tuple(colIdx, colIdx + 1));
}
template <class T, class SliceSpec>
constexpr auto slice(starpu::Matrix<T>& v, SliceSpec&& range, uint32_t colIdx)
{
    return slice(v, std::forward<SliceSpec>(range),
                 std::make_tuple(colIdx, colIdx + 1));
}

template <class T, class SliceSpec>
constexpr auto slice(const starpu::Matrix<T>& v,
                     uint32_t rowIdx,
                     SliceSpec&& range)
{
    return slice(v, std::make_tuple(rowIdx, rowIdx + 1),
                 std::forward<SliceSpec>(range));
}
template <class T, class SliceSpec>
constexpr auto slice(starpu::Matrix<T>& v, uint32_t rowIdx, SliceSpec&& range)
{
    return slice(v, std::make_tuple(rowIdx, rowIdx + 1),
                 std::forward<SliceSpec>(range));
}

template <class T, class SliceSpec>
constexpr auto slice(const starpu::Matrix<T>& v, SliceSpec&& range)
{
    assert((v.nrows() <= 1 || v.ncols() <= 1) && "Matrix is not a vector");

    if (v.nrows() > 1)
        return slice(v, std::forward<SliceSpec>(range), std::make_tuple(0, 1));
    else
        return slice(v, std::make_tuple(0, 1), std::forward<SliceSpec>(range));
}
template <class T, class SliceSpec>
constexpr auto slice(starpu::Matrix<T>& v, SliceSpec&& range)
{
    assert((v.nrows() <= 1 || v.ncols() <= 1) && "Matrix is not a vector");

    if (v.nrows() > 1)
        return slice(v, std::forward<SliceSpec>(range), std::make_tuple(0, 1));
    else
        return slice(v, std::make_tuple(0, 1), std::forward<SliceSpec>(range));
}

template <class T>
constexpr auto col(const starpu::Matrix<T>& A, uint32_t colIdx)
{
    return slice(A, std::make_tuple(0, A.nrows()),
                 std::make_tuple(colIdx, colIdx + 1));
}
template <class T>
constexpr auto col(starpu::Matrix<T>& A, uint32_t colIdx)
{
    return slice(A, std::make_tuple(0, A.nrows()),
                 std::make_tuple(colIdx, colIdx + 1));
}

template <class T, class SliceSpec>
constexpr auto cols(const starpu::Matrix<T>& A, SliceSpec&& cols)
{
    return slice(A, std::make_tuple(0, A.nrows()),
                 std::forward<SliceSpec>(cols));
}
template <class T, class SliceSpec>
constexpr auto cols(starpu::Matrix<T>& A, SliceSpec&& cols)
{
    return slice(A, std::make_tuple(0, A.nrows()),
                 std::forward<SliceSpec>(cols));
}

template <class T>
constexpr auto row(const starpu::Matrix<T>& A, uint32_t rowIdx)
{
    return slice(A, std::make_tuple(rowIdx, rowIdx + 1),
                 std::make_tuple(0, A.ncols()));
}
template <class T>
constexpr auto row(starpu::Matrix<T>& A, uint32_t rowIdx)
{
    return slice(A, std::make_tuple(rowIdx, rowIdx + 1),
                 std::make_tuple(0, A.ncols()));
}

template <class T, class SliceSpec>
constexpr auto rows(const starpu::Matrix<T>& A, SliceSpec&& rows)
{
    return slice(A, std::forward<SliceSpec>(rows),
                 std::make_tuple(0, A.ncols()));
}
template <class T, class SliceSpec>
constexpr auto rows(starpu::Matrix<T>& A, SliceSpec&& rows)
{
    return slice(A, std::forward<SliceSpec>(rows),
                 std::make_tuple(0, A.ncols()));
}

}  // namespace tlapack

#endif  // TLAPACK_STARPU_HH
