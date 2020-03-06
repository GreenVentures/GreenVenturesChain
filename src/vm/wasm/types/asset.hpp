#pragma once

#include "symbol.hpp"
#include<iostream>
#include <cstdlib>

#include "check.hpp"
#include "types.hpp"

#include "wasm/wasm_serialize_reflect.hpp"

namespace wasm {
    /**
     *  Defines C++ API for managing assets
     *  @addtogroup asset Asset C++ API
     *  @ingroup core
     *  @{
     */

    /**
     * @struct Stores information for owner of asset
     */

    struct asset {
        /**
         * The amount of the asset
         */
        int64_t amount = 0;

        /**
         * The symbol name of the asset
         */
        symbol sym;

        /**
         * Maximum amount possible for this asset. It's capped to 2^62 - 1
         */
        static constexpr int64_t max_amount = (1LL << 62) - 1;

        asset() {}

        /**
         * Construct a new asset given the symbol name and the amount
         *
         * @param a - The amount of the asset
         * @param s - The name of the symbol
         */
        asset( int64_t a, class symbol s )
                : amount(a), sym{s} {
            check(is_amount_within_range(), "magnitude of asset amount must be less than 2^62");
            check(sym.is_valid(), "invalid symbol name");
        }

        /**
         * Check if the amount doesn't exceed the max amount
         *
         * @return true - if the amount doesn't exceed the max amount
         * @return false - otherwise
         */
        bool is_amount_within_range() const { return -max_amount <= amount && amount <= max_amount; }

        /**
         * Check if the asset is valid. %A valid asset has its amount <= max_amount and its symbol name valid
         *
         * @return true - if the asset is valid
         * @return false - otherwise
         */
        bool is_valid() const { return is_amount_within_range() && sym.is_valid(); }

        /**
         * Set the amount of the asset
         *
         * @param a - New amount for the asset
         */
        void set_amount( int64_t a ) {
            amount = a;
            check(is_amount_within_range(), "magnitude of asset amount must be less than 2^62");
        }

        /**
         * Unary minus operator
         *
         * @return asset - New asset with its amount is the negative amount of this asset
         */
        asset operator-() const {
            asset r = *this;
            r.amount = -r.amount;
            return r;
        }

        /**
         * Subtraction assignment operator
         *
         * @param a - Another asset to subtract this asset with
         * @return asset& - Reference to this asset
         * @post The amount of this asset is subtracted by the amount of asset a
         */
        asset &operator-=( const asset &a ) {
            check(a.sym == sym, "attempt to subtract asset with different symbol");
            amount -= a.amount;
            check(-max_amount <= amount, "subtraction underflow");
            check(amount <= max_amount, "subtraction overflow");
            return *this;
        }


        /**
         * Addition Assignment  operator
         *
         * @param a - Another asset to subtract this asset with
         * @return asset& - Reference to this asset
         * @post The amount of this asset is added with the amount of asset a
         */
        asset &operator+=( int64_t a ) {
            //check( a.sym == sym, "attempt to add asset with different symbol" );
            amount += a;
            check(-max_amount <= amount, "addition underflow");
            check(amount <= max_amount, "addition overflow");
            return *this;
        }

        /**
         * Addition Assignment  operator
         *
         * @param a - Another asset to subtract this asset with
         * @return asset& - Reference to this asset
         * @post The amount of this asset is added with the amount of asset a
         */
        asset &operator+=( const asset &a ) {
            check(a.sym == sym, "attempt to add asset with different symbol");
            amount += a.amount;
            check(-max_amount <= amount, "addition underflow");
            check(amount <= max_amount, "addition overflow");
            return *this;
        }

        /**
         * Addition operator
         *
         * @param a - The first asset to be added
         * @param b - The second asset to be added
         * @return asset - New asset as the result of addition
         */
        inline friend asset operator+( const asset &a, const asset &b ) {
            asset result = a;
            result += b;
            return result;
        }

        /**
         * Subtraction operator
         *
         * @param a - The asset to be subtracted
         * @param b - The asset used to subtract
         * @return asset - New asset as the result of subtraction of a with b
         */
        inline friend asset operator-( const asset &a, const asset &b ) {
            asset result = a;
            result -= b;
            return result;
        }

        /**
         * @brief Multiplication assignment operator, with a number
         *
         * @details Multiplication assignment operator. Multiply the amount of this asset with a number and then assign the value to itself.
         * @param a - The multiplier for the asset's amount
         * @return asset - Reference to this asset
         * @post The amount of this asset is multiplied by a
         */
        asset &operator*=( int64_t a ) {
            //int64_t tmp = amount * a;
            int128_t tmp = (int128_t)amount * (int128_t)a;
            check( tmp <= max_amount, "multiplication overflow" );
            check( tmp >= -max_amount, "multiplication underflow" );
            amount = (int64_t) tmp;
            return *this;
        }

        /**
         * Multiplication operator, with a number proceeding
         *
         * @brief Multiplication operator, with a number proceeding
         * @param a - The asset to be multiplied
         * @param b - The multiplier for the asset's amount
         * @return asset - New asset as the result of multiplication
         */
        friend asset operator*( const asset &a, int64_t b ) {
            asset result = a;
            result *= b;
            return result;
        }


        /**
         * Multiplication operator, with a number preceeding
         *
         * @param a - The multiplier for the asset's amount
         * @param b - The asset to be multiplied
         * @return asset - New asset as the result of multiplication
         */
        friend asset operator*( int64_t b, const asset &a ) {
            asset result = a;
            result *= b;
            return result;
        }

        /**
         * @brief Division assignment operator, with a number
         *
         * @details Division assignment operator. Divide the amount of this asset with a number and then assign the value to itself.
         * @param a - The divisor for the asset's amount
         * @return asset - Reference to this asset
         * @post The amount of this asset is divided by a
         */
        asset &operator/=( int64_t a ) {
            check(a != 0, "divide by zero");
            check(!(amount == std::numeric_limits<int64_t>::min() && a == -1), "signed division overflow");
            amount /= a;
            return *this;
        }

        /**
         * Division operator, with a number proceeding
         *
         * @param a - The asset to be divided
         * @param b - The divisor for the asset's amount
         * @return asset - New asset as the result of division
         */
        friend asset operator/( const asset &a, int64_t b ) {
            asset result = a;
            result /= b;
            return result;
        }

        /**
         * Division operator, with another asset
         *
         * @param a - The asset which amount acts as the dividend
         * @param b - The asset which amount acts as the divisor
         * @return int64_t - the resulted amount after the division
         * @pre Both asset must have the same symbol
         */
        friend int64_t operator/( const asset &a, const asset &b ) {
            check(b.amount != 0, "divide by zero");
            check(a.sym == b.sym, "comparison of assets with different symbols is not allowed");
            return a.amount / b.amount;
        }

        /**
         * Equality operator
         *
         * @param a - The first asset to be compared
         * @param b - The second asset to be compared
         * @return true - if both asset has the same amount
         * @return false - otherwise
         * @pre Both asset must have the same symbol
         */
        friend bool operator==( const asset &a, const asset &b ) {
            check(a.sym == b.sym, "comparison of assets with different symbols is not allowed");
            return a.amount == b.amount;
        }

        /**
         * Inequality operator
         *
         * @param a - The first asset to be compared
         * @param b - The second asset to be compared
         * @return true - if both asset doesn't have the same amount
         * @return false - otherwise
         * @pre Both asset must have the same symbol
         */
        friend bool operator!=( const asset &a, const asset &b ) {
            return !(a == b);
        }

        /**
         * Less than operator
         *
         * @param a - The first asset to be compared
         * @param b - The second asset to be compared
         * @return true - if the first asset's amount is less than the second asset amount
         * @return false - otherwise
         * @pre Both asset must have the same symbol
         */
        friend bool operator<( const asset &a, const asset &b ) {
            check(a.sym == b.sym, "comparison of assets with different symbols is not allowed");
            return a.amount < b.amount;
        }

        /**
         * Less or equal to operator
         *
         * @param a - The first asset to be compared
         * @param b - The second asset to be compared
         * @return true - if the first asset's amount is less or equal to the second asset amount
         * @return false - otherwise
         * @pre Both asset must have the same symbol
         */
        friend bool operator<=( const asset &a, const asset &b ) {
            check(a.sym == b.sym, "comparison of assets with different symbols is not allowed");
            return a.amount <= b.amount;
        }

        /**
         * Greater than operator
         *
         * @param a - The first asset to be compared
         * @param b - The second asset to be compared
         * @return true - if the first asset's amount is greater than the second asset amount
         * @return false - otherwise
         * @pre Both asset must have the same symbol
         */
        friend bool operator>( const asset &a, const asset &b ) {
            check(a.sym == b.sym, "comparison of assets with different symbols is not allowed");
            return a.amount > b.amount;
        }

        /**
         * Greater or equal to operator
         *
         * @param a - The first asset to be compared
         * @param b - The second asset to be compared
         * @return true - if the first asset's amount is greater or equal to the second asset amount
         * @return false - otherwise
         * @pre Both asset must have the same symbol
         */
        friend bool operator>=( const asset &a, const asset &b ) {
            check(a.sym == b.sym, "comparison of assets with different symbols is not allowed");
            return a.amount >= b.amount;
        }

        /**
         * %asset to std::string
         *
         * @brief %asset to std::string
         */
        std::string to_string() const {
            uint8_t p =  sym.precision();

            //bool negative = false;
            int64_t invert = 1;

            int64_t p10 = sym.precision_in_10();

            p =  sym.precision();

            char fraction[256];
            fraction[p] = '\0';

            if (amount < 0) {
                invert = -1;
                //negative = true;
            }

            auto change = (amount % p10) * invert;

            for (int64_t i = p - 1; i >= 0; --i) {
                fraction[i] = (change % 10) + '0';
                change /= 10;
            }
            char str[256 + 32];
            snprintf(str, sizeof(str), "%ld%s%s %s",
                     (int64_t)(amount / p10),
                     (fraction[0]) ? "." : "",
                     fraction,
                     sym.code().to_string().c_str());
            return {str};
        }


        asset from_string( const string &from ) {

            try {

                string s = wasm::trim(from);

                // Find space in order to split amount and symbol
                auto space_pos = s.find(' ');
                CHAIN_ASSERT((space_pos != string::npos), asset_type_exception, "Asset's amount and symbol should be separated with space. ex. 98.00000000 GVC");
                auto symbol_str = wasm::trim(s.substr(space_pos + 1));
                auto amount_str = s.substr(0, space_pos);

                // Ensure that if decimal point is used (.), decimal fraction is specified
                auto dot_pos = amount_str.find('.');
                if (dot_pos != string::npos) {
                    CHAIN_ASSERT((dot_pos != amount_str.size() - 1), asset_type_exception, "Missing decimal fraction after decimal point. ex. 98.00000000 GVC");
                }

                // Parse symbol
                string precision_digit_str;
                if (dot_pos != string::npos) {
                    //precision_digit_str = eosio::chain::to_string(amount_str.size() - dot_pos - 1);
                    char c[8];
                    sprintf(c, "%ld", amount_str.size() - dot_pos - 1);

                    precision_digit_str = string(c);
                } else {
                    precision_digit_str = "0";
                }

                string symbol_part = precision_digit_str + ',' + symbol_str;
                symbol sym = symbol::from_string(symbol_part);

                // Parse amount
                int64_t int_part, fract_part = 0;
                if (dot_pos != string::npos) {
                    int_part   = atoi(amount_str.substr(0, dot_pos).data());
                    fract_part = atoi(amount_str.substr(dot_pos + 1).data());
                    if (amount_str[0] == '-') fract_part *= -1;
                } else {
                    int_part = atoi(amount_str.data());
                }

                int64_t amount = int_part;
                amount *= sym.precision_in_10();
                amount += fract_part;


                return asset(amount, sym);
            }
            CHAIN_CAPTURE_AND_RETHROW( "%s", from.c_str() )

        }


        WASM_REFLECT( asset, (amount)(sym) )

        // /**
        // *  Serialize a asset into a stream
        // *
        // *  @brief Serialize a asset
        // *  @param ds - The stream to write
        // *  @param sym - The value to serialize
        // *  @tparam DataStream - Type of datastream buffer
        // *  @return DataStream& - Reference to the datastream
        // */
        // template<typename DataStream>
        // friend inline DataStream &operator<<( DataStream &ds, const wasm::asset &asset ) {
        //     ds << asset.amount;
        //     ds << asset.sym;
        //     return ds;
        // }

        // /**
        // *  Deserialize a asset from a stream
        // *
        // *  @brief Deserialize a asset
        // *  @param ds - The stream to read
        // *  @param symbol - The destination for deserialized value
        // *  @tparam DataStream - Type of datastream buffer
        // *  @return DataStream& - Reference to the datastream
        // */
        // template<typename DataStream>
        // friend inline DataStream &operator>>( DataStream &ds, wasm::asset &asset ) {
        //     ds >> asset.amount;
        //     ds >> asset.sym;

        //     return ds;
        // }


    };

/// @} asset type
}
